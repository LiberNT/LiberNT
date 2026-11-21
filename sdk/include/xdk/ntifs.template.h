/*
 * ntifs.h
 *
 * Windows NT Filesystem Driver Developer Kit
 *
 * This file is part of the ReactOS DDK package.
 *
 * Contributors:
 *   Amine Khaldi
 *   Timo Kreuzer (timo.kreuzer@reactos.org)
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#pragma once

#define _NTIFS_INCLUDED_
#define _GNU_NTIFS_

#ifdef __cplusplus
extern "C" {
#endif

$define(UCHAR=UCHAR)
$define(ULONG=ULONG)
$define(USHORT=USHORT)

/* Dependencies */
#include <ntddk.h>
#include <excpt.h>
#include <ntdef.h>
#include <ntnls.h>
#include <ntstatus.h>
#include <bugcodes.h>
#include <ntiologc.h>

$define (_NTIFS_)

#ifndef FlagOn
#define FlagOn(_F,_SF)        ((_F) & (_SF))
#endif

#ifndef BooleanFlagOn
#define BooleanFlagOn(F,SF)   ((BOOLEAN)(((F) & (SF)) != 0))
#endif

#ifndef SetFlag
#define SetFlag(_F,_SF)       ((_F) |= (_SF))
#endif

#ifndef ClearFlag
#define ClearFlag(_F,_SF)     ((_F) &= ~(_SF))
#endif

typedef UNICODE_STRING LSA_UNICODE_STRING, *PLSA_UNICODE_STRING;
typedef STRING LSA_STRING, *PLSA_STRING;
typedef OBJECT_ATTRIBUTES LSA_OBJECT_ATTRIBUTES, *PLSA_OBJECT_ATTRIBUTES;

$include (setypes.h)
$include (obtypes.h)
$include (rtltypes.h)
$include (rtlfuncs.h)

_IRQL_requires_max_(PASSIVE_LEVEL)
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtQueryObject(
  _In_opt_ HANDLE Handle,
  _In_ OBJECT_INFORMATION_CLASS ObjectInformationClass,
  _Out_writes_bytes_opt_(ObjectInformationLength) PVOID ObjectInformation,
  _In_ ULONG ObjectInformationLength,
  _Out_opt_ PULONG ReturnLength);

#if (NTDDI_VERSION >= NTDDI_WIN2K)

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtOpenThreadToken(
  _In_ HANDLE ThreadHandle,
  _In_ ACCESS_MASK DesiredAccess,
  _In_ BOOLEAN OpenAsSelf,
  _Out_ PHANDLE TokenHandle);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtOpenProcessToken(
  _In_ HANDLE ProcessHandle,
  _In_ ACCESS_MASK DesiredAccess,
  _Out_ PHANDLE TokenHandle);

_When_(TokenInformationClass == TokenAccessInformation,
  _At_(TokenInformationLength,
       _In_range_(>=, sizeof(TOKEN_ACCESS_INFORMATION))))
_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtQueryInformationToken(
  _In_ HANDLE TokenHandle,
  _In_ TOKEN_INFORMATION_CLASS TokenInformationClass,
  _Out_writes_bytes_to_opt_(TokenInformationLength, *ReturnLength) PVOID TokenInformation,
  _In_ ULONG TokenInformationLength,
  _Out_ PULONG ReturnLength);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtAdjustPrivilegesToken(
    _In_ HANDLE TokenHandle,
    _In_ BOOLEAN DisableAllPrivileges,
    _In_opt_ PTOKEN_PRIVILEGES NewState,
    _In_ ULONG BufferLength,
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnLength) PTOKEN_PRIVILEGES PreviousState,
    _When_(PreviousState != NULL, _Out_) PULONG ReturnLength);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtCreateFile(
  _Out_ PHANDLE FileHandle,
  _In_ ACCESS_MASK DesiredAccess,
  _In_ POBJECT_ATTRIBUTES ObjectAttributes,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _In_opt_ PLARGE_INTEGER AllocationSize,
  _In_ ULONG FileAttributes,
  _In_ ULONG ShareAccess,
  _In_ ULONG CreateDisposition,
  _In_ ULONG CreateOptions,
  _In_reads_bytes_opt_(EaLength) PVOID EaBuffer,
  _In_ ULONG EaLength);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtDeviceIoControlFile(
  _In_ HANDLE FileHandle,
  _In_opt_ HANDLE Event,
  _In_opt_ PIO_APC_ROUTINE ApcRoutine,
  _In_opt_ PVOID ApcContext,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _In_ ULONG IoControlCode,
  _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
  _In_ ULONG InputBufferLength,
  _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
  _In_ ULONG OutputBufferLength);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtFsControlFile(
  _In_ HANDLE FileHandle,
  _In_opt_ HANDLE Event,
  _In_opt_ PIO_APC_ROUTINE ApcRoutine,
  _In_opt_ PVOID ApcContext,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _In_ ULONG FsControlCode,
  _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
  _In_ ULONG InputBufferLength,
  _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
  _In_ ULONG OutputBufferLength);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtLockFile(
  _In_ HANDLE FileHandle,
  _In_opt_ HANDLE Event,
  _In_opt_ PIO_APC_ROUTINE ApcRoutine,
  _In_opt_ PVOID ApcContext,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _In_ PLARGE_INTEGER ByteOffset,
  _In_ PLARGE_INTEGER Length,
  _In_ ULONG Key,
  _In_ BOOLEAN FailImmediately,
  _In_ BOOLEAN ExclusiveLock);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtOpenFile(
  _Out_ PHANDLE FileHandle,
  _In_ ACCESS_MASK DesiredAccess,
  _In_ POBJECT_ATTRIBUTES ObjectAttributes,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _In_ ULONG ShareAccess,
  _In_ ULONG OpenOptions);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtQueryDirectoryFile(
  _In_ HANDLE FileHandle,
  _In_opt_ HANDLE Event,
  _In_opt_ PIO_APC_ROUTINE ApcRoutine,
  _In_opt_ PVOID ApcContext,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _Out_writes_bytes_(Length) PVOID FileInformation,
  _In_ ULONG Length,
  _In_ FILE_INFORMATION_CLASS FileInformationClass,
  _In_ BOOLEAN ReturnSingleEntry,
  _In_opt_ PUNICODE_STRING FileName,
  _In_ BOOLEAN RestartScan);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtQueryInformationFile(
  _In_ HANDLE FileHandle,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _Out_writes_bytes_(Length) PVOID FileInformation,
  _In_ ULONG Length,
  _In_ FILE_INFORMATION_CLASS FileInformationClass);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtQueryQuotaInformationFile(
  _In_ HANDLE FileHandle,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _Out_writes_bytes_(Length) PVOID Buffer,
  _In_ ULONG Length,
  _In_ BOOLEAN ReturnSingleEntry,
  _In_reads_bytes_opt_(SidListLength) PVOID SidList,
  _In_ ULONG SidListLength,
  _In_reads_bytes_opt_((8 + (4 * ((SID *)StartSid)->SubAuthorityCount))) PSID StartSid,
  _In_ BOOLEAN RestartScan);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtQueryVolumeInformationFile(
  _In_ HANDLE FileHandle,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _Out_writes_bytes_(Length) PVOID FsInformation,
  _In_ ULONG Length,
  _In_ FS_INFORMATION_CLASS FsInformationClass);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtReadFile(
  _In_ HANDLE FileHandle,
  _In_opt_ HANDLE Event,
  _In_opt_ PIO_APC_ROUTINE ApcRoutine,
  _In_opt_ PVOID ApcContext,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _Out_writes_bytes_(Length) PVOID Buffer,
  _In_ ULONG Length,
  _In_opt_ PLARGE_INTEGER ByteOffset,
  _In_opt_ PULONG Key);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtSetInformationFile(
  _In_ HANDLE FileHandle,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _In_reads_bytes_(Length) PVOID FileInformation,
  _In_ ULONG Length,
  _In_ FILE_INFORMATION_CLASS FileInformationClass);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtSetQuotaInformationFile(
  _In_ HANDLE FileHandle,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _In_reads_bytes_(Length) PVOID Buffer,
  _In_ ULONG Length);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtSetVolumeInformationFile(
  _In_ HANDLE FileHandle,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _In_reads_bytes_(Length) PVOID FsInformation,
  _In_ ULONG Length,
  _In_ FS_INFORMATION_CLASS FsInformationClass);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtWriteFile(
  _In_ HANDLE FileHandle,
  _In_opt_ HANDLE Event,
  _In_opt_ PIO_APC_ROUTINE ApcRoutine,
  _In_opt_ PVOID ApcContext,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _In_reads_bytes_(Length) PVOID Buffer,
  _In_ ULONG Length,
  _In_opt_ PLARGE_INTEGER ByteOffset,
  _In_opt_ PULONG Key);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtUnlockFile(
  _In_ HANDLE FileHandle,
  _Out_ PIO_STATUS_BLOCK IoStatusBlock,
  _In_ PLARGE_INTEGER ByteOffset,
  _In_ PLARGE_INTEGER Length,
  _In_ ULONG Key);

_IRQL_requires_max_(PASSIVE_LEVEL)
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtSetSecurityObject(
  _In_ HANDLE Handle,
  _In_ SECURITY_INFORMATION SecurityInformation,
  _In_ PSECURITY_DESCRIPTOR SecurityDescriptor);

_IRQL_requires_max_(PASSIVE_LEVEL)
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtQuerySecurityObject(
  _In_ HANDLE Handle,
  _In_ SECURITY_INFORMATION SecurityInformation,
  _Out_writes_bytes_opt_(Length) PSECURITY_DESCRIPTOR SecurityDescriptor,
  _In_ ULONG Length,
  _Out_ PULONG LengthNeeded);

_IRQL_requires_max_(PASSIVE_LEVEL)
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtClose(
  _In_ HANDLE Handle);

#endif

#if (NTDDI_VERSION >= NTDDI_WINXP)

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtOpenThreadTokenEx(
  _In_ HANDLE ThreadHandle,
  _In_ ACCESS_MASK DesiredAccess,
  _In_ BOOLEAN OpenAsSelf,
  _In_ ULONG HandleAttributes,
  _Out_ PHANDLE TokenHandle);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtOpenProcessTokenEx(
  _In_ HANDLE ProcessHandle,
  _In_ ACCESS_MASK DesiredAccess,
  _In_ ULONG HandleAttributes,
  _Out_ PHANDLE TokenHandle);

_Must_inspect_result_
NTSYSAPI
NTSTATUS
NTAPI
NtOpenJobObjectToken(
  _In_ HANDLE JobHandle,
  _In_ ACCESS_MASK DesiredAccess,
  _Out_ PHANDLE TokenHandle);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtDuplicateToken(
  _In_ HANDLE ExistingTokenHandle,
  _In_ ACCESS_MASK DesiredAccess,
  _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
  _In_ BOOLEAN EffectiveOnly,
  _In_ TOKEN_TYPE TokenType,
  _Out_ PHANDLE NewTokenHandle);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtFilterToken(
  _In_ HANDLE ExistingTokenHandle,
  _In_ ULONG Flags,
  _In_opt_ PTOKEN_GROUPS SidsToDisable,
  _In_opt_ PTOKEN_PRIVILEGES PrivilegesToDelete,
  _In_opt_ PTOKEN_GROUPS RestrictedSids,
  _Out_ PHANDLE NewTokenHandle);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtImpersonateAnonymousToken(
  _In_ HANDLE ThreadHandle);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtSetInformationToken(
  _In_ HANDLE TokenHandle,
  _In_ TOKEN_INFORMATION_CLASS TokenInformationClass,
  _In_reads_bytes_(TokenInformationLength) PVOID TokenInformation,
  _In_ ULONG TokenInformationLength);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtAdjustGroupsToken(
  _In_ HANDLE TokenHandle,
  _In_ BOOLEAN ResetToDefault,
  _In_opt_ PTOKEN_GROUPS NewState,
  _In_opt_ ULONG BufferLength,
  _Out_writes_bytes_to_opt_(BufferLength, *ReturnLength) PTOKEN_GROUPS PreviousState,
  _Out_ PULONG ReturnLength);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtPrivilegeCheck(
  _In_ HANDLE ClientToken,
  _Inout_ PPRIVILEGE_SET RequiredPrivileges,
  _Out_ PBOOLEAN Result);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtAccessCheckAndAuditAlarm(
  _In_ PUNICODE_STRING SubsystemName,
  _In_opt_ PVOID HandleId,
  _In_ PUNICODE_STRING ObjectTypeName,
  _In_ PUNICODE_STRING ObjectName,
  _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
  _In_ ACCESS_MASK DesiredAccess,
  _In_ PGENERIC_MAPPING GenericMapping,
  _In_ BOOLEAN ObjectCreation,
  _Out_ PACCESS_MASK GrantedAccess,
  _Out_ PNTSTATUS AccessStatus,
  _Out_ PBOOLEAN GenerateOnClose);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtAccessCheckByTypeAndAuditAlarm(
  _In_ PUNICODE_STRING SubsystemName,
  _In_opt_ PVOID HandleId,
  _In_ PUNICODE_STRING ObjectTypeName,
  _In_ PUNICODE_STRING ObjectName,
  _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
  _In_opt_ PSID PrincipalSelfSid,
  _In_ ACCESS_MASK DesiredAccess,
  _In_ AUDIT_EVENT_TYPE AuditType,
  _In_ ULONG Flags,
  _In_reads_opt_(ObjectTypeLength) POBJECT_TYPE_LIST ObjectTypeList,
  _In_ ULONG ObjectTypeLength,
  _In_ PGENERIC_MAPPING GenericMapping,
  _In_ BOOLEAN ObjectCreation,
  _Out_ PACCESS_MASK GrantedAccess,
  _Out_ PNTSTATUS AccessStatus,
  _Out_ PBOOLEAN GenerateOnClose);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtAccessCheckByTypeResultListAndAuditAlarm(
  _In_ PUNICODE_STRING SubsystemName,
  _In_opt_ PVOID HandleId,
  _In_ PUNICODE_STRING ObjectTypeName,
  _In_ PUNICODE_STRING ObjectName,
  _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
  _In_opt_ PSID PrincipalSelfSid,
  _In_ ACCESS_MASK DesiredAccess,
  _In_ AUDIT_EVENT_TYPE AuditType,
  _In_ ULONG Flags,
  _In_reads_opt_(ObjectTypeListLength) POBJECT_TYPE_LIST ObjectTypeList,
  _In_ ULONG ObjectTypeListLength,
  _In_ PGENERIC_MAPPING GenericMapping,
  _In_ BOOLEAN ObjectCreation,
  _Out_writes_(ObjectTypeListLength) PACCESS_MASK GrantedAccess,
  _Out_writes_(ObjectTypeListLength) PNTSTATUS AccessStatus,
  _Out_ PBOOLEAN GenerateOnClose);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtAccessCheckByTypeResultListAndAuditAlarmByHandle(
  _In_ PUNICODE_STRING SubsystemName,
  _In_opt_ PVOID HandleId,
  _In_ HANDLE ClientToken,
  _In_ PUNICODE_STRING ObjectTypeName,
  _In_ PUNICODE_STRING ObjectName,
  _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
  _In_opt_ PSID PrincipalSelfSid,
  _In_ ACCESS_MASK DesiredAccess,
  _In_ AUDIT_EVENT_TYPE AuditType,
  _In_ ULONG Flags,
  _In_reads_opt_(ObjectTypeListLength) POBJECT_TYPE_LIST ObjectTypeList,
  _In_ ULONG ObjectTypeListLength,
  _In_ PGENERIC_MAPPING GenericMapping,
  _In_ BOOLEAN ObjectCreation,
  _Out_writes_(ObjectTypeListLength) PACCESS_MASK GrantedAccess,
  _Out_writes_(ObjectTypeListLength) PNTSTATUS AccessStatus,
  _Out_ PBOOLEAN GenerateOnClose);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtOpenObjectAuditAlarm(
  _In_ PUNICODE_STRING SubsystemName,
  _In_opt_ PVOID HandleId,
  _In_ PUNICODE_STRING ObjectTypeName,
  _In_ PUNICODE_STRING ObjectName,
  _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
  _In_ HANDLE ClientToken,
  _In_ ACCESS_MASK DesiredAccess,
  _In_ ACCESS_MASK GrantedAccess,
  _In_opt_ PPRIVILEGE_SET Privileges,
  _In_ BOOLEAN ObjectCreation,
  _In_ BOOLEAN AccessGranted,
  _Out_ PBOOLEAN GenerateOnClose);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtPrivilegeObjectAuditAlarm(
  _In_ PUNICODE_STRING SubsystemName,
  _In_opt_ PVOID HandleId,
  _In_ HANDLE ClientToken,
  _In_ ACCESS_MASK DesiredAccess,
  _In_ PPRIVILEGE_SET Privileges,
  _In_ BOOLEAN AccessGranted);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtCloseObjectAuditAlarm(
  _In_ PUNICODE_STRING SubsystemName,
  _In_opt_ PVOID HandleId,
  _In_ BOOLEAN GenerateOnClose);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtDeleteObjectAuditAlarm(
  _In_ PUNICODE_STRING SubsystemName,
  _In_opt_ PVOID HandleId,
  _In_ BOOLEAN GenerateOnClose);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtPrivilegedServiceAuditAlarm(
  _In_ PUNICODE_STRING SubsystemName,
  _In_ PUNICODE_STRING ServiceName,
  _In_ HANDLE ClientToken,
  _In_ PPRIVILEGE_SET Privileges,
  _In_ BOOLEAN AccessGranted);

__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtSetInformationThread(
  _In_ HANDLE ThreadHandle,
  _In_ THREADINFOCLASS ThreadInformationClass,
  _In_reads_bytes_(ThreadInformationLength) PVOID ThreadInformation,
  _In_ ULONG ThreadInformationLength);

_Must_inspect_result_
__kernel_entry
NTSYSCALLAPI
NTSTATUS
NTAPI
NtCreateSection(
  _Out_ PHANDLE SectionHandle,
  _In_ ACCESS_MASK DesiredAccess,
  _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
  _In_opt_ PLARGE_INTEGER MaximumSize,
  _In_ ULONG SectionPageProtection,
  _In_ ULONG AllocationAttributes,
  _In_opt_ HANDLE FileHandle);

#endif

#define COMPRESSION_FORMAT_NONE         (0x0000)
#define COMPRESSION_FORMAT_DEFAULT      (0x0001)
#define COMPRESSION_FORMAT_LZNT1        (0x0002)
#define COMPRESSION_ENGINE_STANDARD     (0x0000)
#define COMPRESSION_ENGINE_MAXIMUM      (0x0100)
#define COMPRESSION_ENGINE_HIBER        (0x0200)

#define MAX_UNICODE_STACK_BUFFER_LENGTH 256

#define METHOD_FROM_CTL_CODE(ctrlCode)  ((ULONG)(ctrlCode & 3))

#define METHOD_DIRECT_TO_HARDWARE       METHOD_IN_DIRECT
#define METHOD_DIRECT_FROM_HARDWARE     METHOD_OUT_DIRECT

typedef ULONG LSA_OPERATIONAL_MODE, *PLSA_OPERATIONAL_MODE;

typedef enum _SECURITY_LOGON_TYPE {
  UndefinedLogonType = 0,
  Interactive = 2,
  Network,
  Batch,
  Service,
  Proxy,
  Unlock,
  NetworkCleartext,
  NewCredentials,
#if (_WIN32_WINNT >= 0x0501)
  RemoteInteractive,
  CachedInteractive,
#endif
#if (_WIN32_WINNT >= 0x0502)
  CachedRemoteInteractive,
  CachedUnlock
#endif
} SECURITY_LOGON_TYPE, *PSECURITY_LOGON_TYPE;

#ifndef _NTLSA_AUDIT_
#define _NTLSA_AUDIT_

#ifndef GUID_DEFINED
#include <guiddef.h>
#endif

#endif /* _NTLSA_AUDIT_ */

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
LsaRegisterLogonProcess(
  _In_ PLSA_STRING LogonProcessName,
  _Out_ PHANDLE LsaHandle,
  _Out_ PLSA_OPERATIONAL_MODE SecurityMode);

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
LsaLogonUser(
  _In_ HANDLE LsaHandle,
  _In_ PLSA_STRING OriginName,
  _In_ SECURITY_LOGON_TYPE LogonType,
  _In_ ULONG AuthenticationPackage,
  _In_reads_bytes_(AuthenticationInformationLength) PVOID AuthenticationInformation,
  _In_ ULONG AuthenticationInformationLength,
  _In_opt_ PTOKEN_GROUPS LocalGroups,
  _In_ PTOKEN_SOURCE SourceContext,
  _Out_ PVOID *ProfileBuffer,
  _Out_ PULONG ProfileBufferLength,
  _Inout_ PLUID LogonId,
  _Out_ PHANDLE Token,
  _Out_ PQUOTA_LIMITS Quotas,
  _Out_ PNTSTATUS SubStatus);

_IRQL_requires_same_
NTSTATUS
NTAPI
LsaFreeReturnBuffer(
  _In_ PVOID Buffer);

#ifndef _NTLSA_IFS_
#define _NTLSA_IFS_
#endif

#define MSV1_0_PACKAGE_NAME     "MICROSOFT_AUTHENTICATION_PACKAGE_V1_0"
#define MSV1_0_PACKAGE_NAMEW    L"MICROSOFT_AUTHENTICATION_PACKAGE_V1_0"
#define MSV1_0_PACKAGE_NAMEW_LENGTH sizeof(MSV1_0_PACKAGE_NAMEW) - sizeof(WCHAR)

#define MSV1_0_SUBAUTHENTICATION_KEY "SYSTEM\\CurrentControlSet\\Control\\Lsa\\MSV1_0"
#define MSV1_0_SUBAUTHENTICATION_VALUE "Auth"

#define MSV1_0_CHALLENGE_LENGTH                8
#define MSV1_0_USER_SESSION_KEY_LENGTH         16
#define MSV1_0_LANMAN_SESSION_KEY_LENGTH       8

#define MSV1_0_CLEARTEXT_PASSWORD_ALLOWED      0x02
#define MSV1_0_UPDATE_LOGON_STATISTICS         0x04
#define MSV1_0_RETURN_USER_PARAMETERS          0x08
#define MSV1_0_DONT_TRY_GUEST_ACCOUNT          0x10
#define MSV1_0_ALLOW_SERVER_TRUST_ACCOUNT      0x20
#define MSV1_0_RETURN_PASSWORD_EXPIRY          0x40
#define MSV1_0_USE_CLIENT_CHALLENGE            0x80
#define MSV1_0_TRY_GUEST_ACCOUNT_ONLY          0x100
#define MSV1_0_RETURN_PROFILE_PATH             0x200
#define MSV1_0_TRY_SPECIFIED_DOMAIN_ONLY       0x400
#define MSV1_0_ALLOW_WORKSTATION_TRUST_ACCOUNT 0x800

#define MSV1_0_DISABLE_PERSONAL_FALLBACK     0x00001000
#define MSV1_0_ALLOW_FORCE_GUEST             0x00002000

#if (_WIN32_WINNT >= 0x0502)
#define MSV1_0_CLEARTEXT_PASSWORD_SUPPLIED   0x00004000
#define MSV1_0_USE_DOMAIN_FOR_ROUTING_ONLY   0x00008000
#endif

#define MSV1_0_SUBAUTHENTICATION_DLL_EX      0x00100000
#define MSV1_0_ALLOW_MSVCHAPV2               0x00010000

#if (_WIN32_WINNT >= 0x0600)
#define MSV1_0_S4U2SELF                      0x00020000
#define MSV1_0_CHECK_LOGONHOURS_FOR_S4U      0x00040000
#endif

#define MSV1_0_SUBAUTHENTICATION_DLL         0xFF000000
#define MSV1_0_SUBAUTHENTICATION_DLL_SHIFT   24
#define MSV1_0_MNS_LOGON                     0x01000000

#define MSV1_0_SUBAUTHENTICATION_DLL_RAS     2
#define MSV1_0_SUBAUTHENTICATION_DLL_IIS     132

#define LOGON_GUEST                 0x01
#define LOGON_NOENCRYPTION          0x02
#define LOGON_CACHED_ACCOUNT        0x04
#define LOGON_USED_LM_PASSWORD      0x08
#define LOGON_EXTRA_SIDS            0x20
#define LOGON_SUBAUTH_SESSION_KEY   0x40
#define LOGON_SERVER_TRUST_ACCOUNT  0x80
#define LOGON_NTLMV2_ENABLED        0x100
#define LOGON_RESOURCE_GROUPS       0x200
#define LOGON_PROFILE_PATH_RETURNED 0x400
#define LOGON_NT_V2                 0x800
#define LOGON_LM_V2                 0x1000
#define LOGON_NTLM_V2               0x2000

#if (_WIN32_WINNT >= 0x0600)

#define LOGON_OPTIMIZED             0x4000
#define LOGON_WINLOGON              0x8000
#define LOGON_PKINIT               0x10000
#define LOGON_NO_OPTIMIZED         0x20000

#endif

#define MSV1_0_SUBAUTHENTICATION_FLAGS 0xFF000000

#define LOGON_GRACE_LOGON              0x01000000

#define MSV1_0_OWF_PASSWORD_LENGTH 16
#define MSV1_0_CRED_LM_PRESENT 0x1
#define MSV1_0_CRED_NT_PRESENT 0x2
#define MSV1_0_CRED_VERSION 0

#define MSV1_0_NTLM3_RESPONSE_LENGTH 16
#define MSV1_0_NTLM3_OWF_LENGTH 16

#if (_WIN32_WINNT == 0x0500)
#define MSV1_0_MAX_NTLM3_LIFE 1800
#else
#define MSV1_0_MAX_NTLM3_LIFE 129600
#endif
#define MSV1_0_MAX_AVL_SIZE 64000

#if (_WIN32_WINNT >= 0x0501)

#define MSV1_0_AV_FLAG_FORCE_GUEST                  0x00000001

#if (_WIN32_WINNT >= 0x0600)
#define MSV1_0_AV_FLAG_MIC_HANDSHAKE_MESSAGES       0x00000002
#endif

#endif

#define MSV1_0_NTLM3_INPUT_LENGTH (sizeof(MSV1_0_NTLM3_RESPONSE) - MSV1_0_NTLM3_RESPONSE_LENGTH)

#if(_WIN32_WINNT >= 0x0502)
#define MSV1_0_NTLM3_MIN_NT_RESPONSE_LENGTH RTL_SIZEOF_THROUGH_FIELD(MSV1_0_NTLM3_RESPONSE, AvPairsOff)
#endif

#define USE_PRIMARY_PASSWORD            0x01
#define RETURN_PRIMARY_USERNAME         0x02
#define RETURN_PRIMARY_LOGON_DOMAINNAME 0x04
#define RETURN_NON_NT_USER_SESSION_KEY  0x08
#define GENERATE_CLIENT_CHALLENGE       0x10
#define GCR_NTLM3_PARMS                 0x20
#define GCR_TARGET_INFO                 0x40
#define RETURN_RESERVED_PARAMETER       0x80
#define GCR_ALLOW_NTLM                 0x100
#define GCR_USE_OEM_SET                0x200
#define GCR_MACHINE_CREDENTIAL         0x400
#define GCR_USE_OWF_PASSWORD           0x800
#define GCR_ALLOW_LM                  0x1000
#define GCR_ALLOW_NO_TARGET           0x2000

typedef enum _MSV1_0_LOGON_SUBMIT_TYPE {
  MsV1_0InteractiveLogon = 2,
  MsV1_0Lm20Logon,
  MsV1_0NetworkLogon,
  MsV1_0SubAuthLogon,
  MsV1_0WorkstationUnlockLogon = 7,
  MsV1_0S4ULogon = 12,
  MsV1_0VirtualLogon = 82
} MSV1_0_LOGON_SUBMIT_TYPE, *PMSV1_0_LOGON_SUBMIT_TYPE;

typedef enum _MSV1_0_PROFILE_BUFFER_TYPE {
  MsV1_0InteractiveProfile = 2,
  MsV1_0Lm20LogonProfile,
  MsV1_0SmartCardProfile
} MSV1_0_PROFILE_BUFFER_TYPE, *PMSV1_0_PROFILE_BUFFER_TYPE;

typedef struct _MSV1_0_INTERACTIVE_LOGON {
  MSV1_0_LOGON_SUBMIT_TYPE MessageType;
  UNICODE_STRING LogonDomainName;
  UNICODE_STRING UserName;
  UNICODE_STRING Password;
} MSV1_0_INTERACTIVE_LOGON, *PMSV1_0_INTERACTIVE_LOGON;

typedef struct _MSV1_0_INTERACTIVE_PROFILE {
  MSV1_0_PROFILE_BUFFER_TYPE MessageType;
  USHORT LogonCount;
  USHORT BadPasswordCount;
  LARGE_INTEGER LogonTime;
  LARGE_INTEGER LogoffTime;
  LARGE_INTEGER KickOffTime;
  LARGE_INTEGER PasswordLastSet;
  LARGE_INTEGER PasswordCanChange;
  LARGE_INTEGER PasswordMustChange;
  UNICODE_STRING LogonScript;
  UNICODE_STRING HomeDirectory;
  UNICODE_STRING FullName;
  UNICODE_STRING ProfilePath;
  UNICODE_STRING HomeDirectoryDrive;
  UNICODE_STRING LogonServer;
  ULONG UserFlags;
} MSV1_0_INTERACTIVE_PROFILE, *PMSV1_0_INTERACTIVE_PROFILE;

typedef struct _MSV1_0_LM20_LOGON {
  MSV1_0_LOGON_SUBMIT_TYPE MessageType;
  UNICODE_STRING LogonDomainName;
  UNICODE_STRING UserName;
  UNICODE_STRING Workstation;
  UCHAR ChallengeToClient[MSV1_0_CHALLENGE_LENGTH];
  STRING CaseSensitiveChallengeResponse;
  STRING CaseInsensitiveChallengeResponse;
  ULONG ParameterControl;
} MSV1_0_LM20_LOGON, * PMSV1_0_LM20_LOGON;

typedef struct _MSV1_0_SUBAUTH_LOGON {
  MSV1_0_LOGON_SUBMIT_TYPE MessageType;
  UNICODE_STRING LogonDomainName;
  UNICODE_STRING UserName;
  UNICODE_STRING Workstation;
  UCHAR ChallengeToClient[MSV1_0_CHALLENGE_LENGTH];
  STRING AuthenticationInfo1;
  STRING AuthenticationInfo2;
  ULONG ParameterControl;
  ULONG SubAuthPackageId;
} MSV1_0_SUBAUTH_LOGON, * PMSV1_0_SUBAUTH_LOGON;

#if (_WIN32_WINNT >= 0x0600)

#define MSV1_0_S4U_LOGON_FLAG_CHECK_LOGONHOURS 0x2

typedef struct _MSV1_0_S4U_LOGON {
  MSV1_0_LOGON_SUBMIT_TYPE MessageType;
  ULONG Flags;
  UNICODE_STRING UserPrincipalName;
  UNICODE_STRING DomainName;
} MSV1_0_S4U_LOGON, *PMSV1_0_S4U_LOGON;

#endif

typedef struct _MSV1_0_LM20_LOGON_PROFILE {
  MSV1_0_PROFILE_BUFFER_TYPE MessageType;
  LARGE_INTEGER KickOffTime;
  LARGE_INTEGER LogoffTime;
  ULONG UserFlags;
  UCHAR UserSessionKey[MSV1_0_USER_SESSION_KEY_LENGTH];
  UNICODE_STRING LogonDomainName;
  UCHAR LanmanSessionKey[MSV1_0_LANMAN_SESSION_KEY_LENGTH];
  UNICODE_STRING LogonServer;
  UNICODE_STRING UserParameters;
} MSV1_0_LM20_LOGON_PROFILE, * PMSV1_0_LM20_LOGON_PROFILE;

typedef struct _MSV1_0_SUPPLEMENTAL_CREDENTIAL {
  ULONG Version;
  ULONG Flags;
  UCHAR LmPassword[MSV1_0_OWF_PASSWORD_LENGTH];
  UCHAR NtPassword[MSV1_0_OWF_PASSWORD_LENGTH];
} MSV1_0_SUPPLEMENTAL_CREDENTIAL, *PMSV1_0_SUPPLEMENTAL_CREDENTIAL;

typedef struct _MSV1_0_NTLM3_RESPONSE {
  UCHAR Response[MSV1_0_NTLM3_RESPONSE_LENGTH];
  UCHAR RespType;
  UCHAR HiRespType;
  USHORT Flags;
  ULONG MsgWord;
  ULONGLONG TimeStamp;
  UCHAR ChallengeFromClient[MSV1_0_CHALLENGE_LENGTH];
  ULONG AvPairsOff;
  UCHAR Buffer[1];
} MSV1_0_NTLM3_RESPONSE, *PMSV1_0_NTLM3_RESPONSE;

typedef enum _MSV1_0_AVID {
  MsvAvEOL,
  MsvAvNbComputerName,
  MsvAvNbDomainName,
  MsvAvDnsComputerName,
  MsvAvDnsDomainName,
#if (_WIN32_WINNT >= 0x0501)
  MsvAvDnsTreeName,
  MsvAvFlags,
#if (_WIN32_WINNT >= 0x0600)
  MsvAvTimestamp,
  MsvAvRestrictions,
  MsvAvTargetName,
  MsvAvChannelBindings,
#endif
#endif
} MSV1_0_AVID;

typedef struct _MSV1_0_AV_PAIR {
  USHORT AvId;
  USHORT AvLen;
} MSV1_0_AV_PAIR, *PMSV1_0_AV_PAIR;

typedef enum _MSV1_0_PROTOCOL_MESSAGE_TYPE {
  MsV1_0Lm20ChallengeRequest = 0,
  MsV1_0Lm20GetChallengeResponse,
  MsV1_0EnumerateUsers,
  MsV1_0GetUserInfo,
  MsV1_0ReLogonUsers,
  MsV1_0ChangePassword,
  MsV1_0ChangeCachedPassword,
  MsV1_0GenericPassthrough,
  MsV1_0CacheLogon,
  MsV1_0SubAuth,
  MsV1_0DeriveCredential,
  MsV1_0CacheLookup,
#if (_WIN32_WINNT >= 0x0501)
  MsV1_0SetProcessOption,
#endif
#if (_WIN32_WINNT >= 0x0600)
  MsV1_0ConfigLocalAliases,
  MsV1_0ClearCachedCredentials,
#endif
} MSV1_0_PROTOCOL_MESSAGE_TYPE, *PMSV1_0_PROTOCOL_MESSAGE_TYPE;

typedef struct _MSV1_0_LM20_CHALLENGE_REQUEST {
  MSV1_0_PROTOCOL_MESSAGE_TYPE MessageType;
} MSV1_0_LM20_CHALLENGE_REQUEST, *PMSV1_0_LM20_CHALLENGE_REQUEST;

typedef struct _MSV1_0_LM20_CHALLENGE_RESPONSE {
  MSV1_0_PROTOCOL_MESSAGE_TYPE MessageType;
  UCHAR ChallengeToClient[MSV1_0_CHALLENGE_LENGTH];
} MSV1_0_LM20_CHALLENGE_RESPONSE, *PMSV1_0_LM20_CHALLENGE_RESPONSE;

typedef struct _MSV1_0_GETCHALLENRESP_REQUEST_V1 {
  MSV1_0_PROTOCOL_MESSAGE_TYPE MessageType;
  ULONG ParameterControl;
  LUID LogonId;
  UNICODE_STRING Password;
  UCHAR ChallengeToClient[MSV1_0_CHALLENGE_LENGTH];
} MSV1_0_GETCHALLENRESP_REQUEST_V1, *PMSV1_0_GETCHALLENRESP_REQUEST_V1;

typedef struct _MSV1_0_GETCHALLENRESP_REQUEST {
  MSV1_0_PROTOCOL_MESSAGE_TYPE MessageType;
  ULONG ParameterControl;
  LUID LogonId;
  UNICODE_STRING Password;
  UCHAR ChallengeToClient[MSV1_0_CHALLENGE_LENGTH];
  UNICODE_STRING UserName;
  UNICODE_STRING LogonDomainName;
  UNICODE_STRING ServerName;
} MSV1_0_GETCHALLENRESP_REQUEST, *PMSV1_0_GETCHALLENRESP_REQUEST;

typedef struct _MSV1_0_GETCHALLENRESP_RESPONSE {
  MSV1_0_PROTOCOL_MESSAGE_TYPE MessageType;
  STRING CaseSensitiveChallengeResponse;
  STRING CaseInsensitiveChallengeResponse;
  UNICODE_STRING UserName;
  UNICODE_STRING LogonDomainName;
  UCHAR UserSessionKey[MSV1_0_USER_SESSION_KEY_LENGTH];
  UCHAR LanmanSessionKey[MSV1_0_LANMAN_SESSION_KEY_LENGTH];
} MSV1_0_GETCHALLENRESP_RESPONSE, *PMSV1_0_GETCHALLENRESP_RESPONSE;

typedef struct _MSV1_0_ENUMUSERS_REQUEST {
  MSV1_0_PROTOCOL_MESSAGE_TYPE MessageType;
} MSV1_0_ENUMUSERS_REQUEST, *PMSV1_0_ENUMUSERS_REQUEST;

typedef struct _MSV1_0_ENUMUSERS_RESPONSE {
  MSV1_0_PROTOCOL_MESSAGE_TYPE MessageType;
  ULONG NumberOfLoggedOnUsers;
  PLUID LogonIds;
  PULONG EnumHandles;
} MSV1_0_ENUMUSERS_RESPONSE, *PMSV1_0_ENUMUSERS_RESPONSE;

typedef struct _MSV1_0_GETUSERINFO_REQUEST {
  MSV1_0_PROTOCOL_MESSAGE_TYPE MessageType;
  LUID LogonId;
} MSV1_0_GETUSERINFO_REQUEST, *PMSV1_0_GETUSERINFO_REQUEST;

typedef struct _MSV1_0_GETUSERINFO_RESPONSE {
  MSV1_0_PROTOCOL_MESSAGE_TYPE MessageType;
  PSID UserSid;
  UNICODE_STRING UserName;
  UNICODE_STRING LogonDomainName;
  UNICODE_STRING LogonServer;
  SECURITY_LOGON_TYPE LogonType;
} MSV1_0_GETUSERINFO_RESPONSE, *PMSV1_0_GETUSERINFO_RESPONSE;

$include (iotypes.h)

typedef struct _PUBLIC_OBJECT_BASIC_INFORMATION {
  ULONG Attributes;
  ACCESS_MASK GrantedAccess;
  ULONG HandleCount;
  ULONG PointerCount;
  ULONG Reserved[10];
} PUBLIC_OBJECT_BASIC_INFORMATION, *PPUBLIC_OBJECT_BASIC_INFORMATION;

typedef struct _PUBLIC_OBJECT_TYPE_INFORMATION {
  UNICODE_STRING TypeName;
  ULONG Reserved [22];
} PUBLIC_OBJECT_TYPE_INFORMATION, *PPUBLIC_OBJECT_TYPE_INFORMATION;

#define SYSTEM_PAGE_PRIORITY_BITS       3
#define SYSTEM_PAGE_PRIORITY_LEVELS     (1 << SYSTEM_PAGE_PRIORITY_BITS)

$include (ketypes.h)
$include (kefuncs.h)
$include (extypes.h)
$include (exfuncs.h)
$include (sefuncs.h)
$include (psfuncs.h)
$include (iofuncs.h)
$include (potypes.h)
$include (pofuncs.h)
$include (mmtypes.h)
$include (mmfuncs.h)
$include (obfuncs.h)
$include (fsrtltypes.h)
$include (fsrtlfuncs.h)
$include (cctypes.h)
$include (ccfuncs.h)
$include (zwfuncs.h)
$include (sspi.h)

/* #if !defined(_X86AMD64_)  FIXME : WHAT ?! */
#if defined(_WIN64)
C_ASSERT(sizeof(ERESOURCE) == 0x68);
C_ASSERT(FIELD_OFFSET(ERESOURCE,ActiveCount) == 0x18);
C_ASSERT(FIELD_OFFSET(ERESOURCE,Flag) == 0x1a);
#else
C_ASSERT(sizeof(ERESOURCE) == 0x38);
C_ASSERT(FIELD_OFFSET(ERESOURCE,ActiveCount) == 0x0c);
C_ASSERT(FIELD_OFFSET(ERESOURCE,Flag) == 0x0e);
#endif
/* #endif */

#if defined(_IA64_)
#if (NTDDI_VERSION >= NTDDI_WIN2K)
//DECLSPEC_DEPRECATED_DDK
NTHALAPI
ULONG
NTAPI
HalGetDmaAlignmentRequirement(
  VOID);
#endif
#endif

#if defined(_M_IX86) || defined(_M_AMD64)
#define HalGetDmaAlignmentRequirement() 1L
#endif

#ifdef _NTSYSTEM_
extern PUSHORT NlsOemLeadByteInfo;
#define NLS_OEM_LEAD_BYTE_INFO NlsOemLeadByteInfo
#else
__CREATE_NTOS_DATA_IMPORT_ALIAS(NlsOemLeadByteInfo)
extern PUSHORT *NlsOemLeadByteInfo;
#define NLS_OEM_LEAD_BYTE_INFO (*NlsOemLeadByteInfo)
#endif

#if (NTDDI_VERSION >= NTDDI_VISTA)

typedef enum _NETWORK_OPEN_LOCATION_QUALIFIER {
  NetworkOpenLocationAny,
  NetworkOpenLocationRemote,
  NetworkOpenLocationLoopback
} NETWORK_OPEN_LOCATION_QUALIFIER;

typedef enum _NETWORK_OPEN_INTEGRITY_QUALIFIER {
  NetworkOpenIntegrityAny,
  NetworkOpenIntegrityNone,
  NetworkOpenIntegritySigned,
  NetworkOpenIntegrityEncrypted,
  NetworkOpenIntegrityMaximum
} NETWORK_OPEN_INTEGRITY_QUALIFIER;

#if (NTDDI_VERSION >= NTDDI_WIN7)

#define NETWORK_OPEN_ECP_IN_FLAG_DISABLE_HANDLE_COLLAPSING 0x1
#define NETWORK_OPEN_ECP_IN_FLAG_DISABLE_HANDLE_DURABILITY 0x2
#define NETWORK_OPEN_ECP_IN_FLAG_FORCE_BUFFERED_SYNCHRONOUS_IO_HACK 0x80000000

typedef struct _NETWORK_OPEN_ECP_CONTEXT {
  USHORT Size;
  USHORT Reserved;
  _ANONYMOUS_STRUCT struct {
    struct {
      NETWORK_OPEN_LOCATION_QUALIFIER Location;
      NETWORK_OPEN_INTEGRITY_QUALIFIER Integrity;
      ULONG Flags;
    } in;
    struct {
      NETWORK_OPEN_LOCATION_QUALIFIER Location;
      NETWORK_OPEN_INTEGRITY_QUALIFIER Integrity;
      ULONG Flags;
    } out;
  } DUMMYSTRUCTNAME;
} NETWORK_OPEN_ECP_CONTEXT, *PNETWORK_OPEN_ECP_CONTEXT;

typedef struct _NETWORK_OPEN_ECP_CONTEXT_V0 {
  USHORT Size;
  USHORT Reserved;
  _ANONYMOUS_STRUCT struct {
    struct {
    NETWORK_OPEN_LOCATION_QUALIFIER Location;
    NETWORK_OPEN_INTEGRITY_QUALIFIER Integrity;
    } in;
    struct {
      NETWORK_OPEN_LOCATION_QUALIFIER Location;
      NETWORK_OPEN_INTEGRITY_QUALIFIER Integrity;
    } out;
  } DUMMYSTRUCTNAME;
} NETWORK_OPEN_ECP_CONTEXT_V0, *PNETWORK_OPEN_ECP_CONTEXT_V0;

#elif (NTDDI_VERSION >= NTDDI_VISTA)
typedef struct _NETWORK_OPEN_ECP_CONTEXT {
  USHORT Size;
  USHORT Reserved;
  _ANONYMOUS_STRUCT struct {
    struct {
      NETWORK_OPEN_LOCATION_QUALIFIER Location;
      NETWORK_OPEN_INTEGRITY_QUALIFIER Integrity;
    } in;
    struct {
      NETWORK_OPEN_LOCATION_QUALIFIER Location;
      NETWORK_OPEN_INTEGRITY_QUALIFIER Integrity;
    } out;
  } DUMMYSTRUCTNAME;
} NETWORK_OPEN_ECP_CONTEXT, *PNETWORK_OPEN_ECP_CONTEXT;
#endif

DEFINE_GUID(GUID_ECP_NETWORK_OPEN_CONTEXT, 0xc584edbf, 0x00df, 0x4d28, 0xb8, 0x84, 0x35, 0xba, 0xca, 0x89, 0x11, 0xe8);

#endif /* (NTDDI_VERSION >= NTDDI_VISTA) */


#if (NTDDI_VERSION >= NTDDI_VISTA)

typedef struct _PREFETCH_OPEN_ECP_CONTEXT {
  PVOID Context;
} PREFETCH_OPEN_ECP_CONTEXT, *PPREFETCH_OPEN_ECP_CONTEXT;

DEFINE_GUID(GUID_ECP_PREFETCH_OPEN, 0xe1777b21, 0x847e, 0x4837, 0xaa, 0x45, 0x64, 0x16, 0x1d, 0x28, 0x6, 0x55);

#endif /* (NTDDI_VERSION >= NTDDI_VISTA) */

#if (NTDDI_VERSION >= NTDDI_WIN7)

DEFINE_GUID (GUID_ECP_NFS_OPEN, 0xf326d30c, 0xe5f8, 0x4fe7, 0xab, 0x74, 0xf5, 0xa3, 0x19, 0x6d, 0x92, 0xdb);
DEFINE_GUID (GUID_ECP_SRV_OPEN, 0xbebfaebc, 0xaabf, 0x489d, 0x9d, 0x2c, 0xe9, 0xe3, 0x61, 0x10, 0x28, 0x53);

typedef struct sockaddr_storage *PSOCKADDR_STORAGE_NFS;

typedef struct _NFS_OPEN_ECP_CONTEXT {
  PUNICODE_STRING ExportAlias;
  PSOCKADDR_STORAGE_NFS ClientSocketAddress;
} NFS_OPEN_ECP_CONTEXT, *PNFS_OPEN_ECP_CONTEXT, **PPNFS_OPEN_ECP_CONTEXT;

#define SRV_OPEN_ECP_CONTEXT_VERSION_2  2

typedef enum _SRV_INSTANCE_TYPE {
  SrvInstanceTypeUndefined = 0,
  SrvInstanceTypePrimary,
  SrvInstanceTypeCsv,
  SrvInstanceTypeSBL,
  SrvInstanceTypeSR,
  SrvInstanceTypeVSMB
} SRV_INSTANCE_TYPE, *PSRV_INSTANCE_TYPE;

typedef struct _SRV_OPEN_ECP_CONTEXT {
  PUNICODE_STRING ShareName;
  PSOCKADDR_STORAGE_NFS SocketAddress;
  BOOLEAN OplockBlockState;
  BOOLEAN OplockAppState;
  BOOLEAN OplockFinalState;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
  USHORT Version;
  SRV_INSTANCE_TYPE InstanceType;
#endif
} SRV_OPEN_ECP_CONTEXT, *PSRV_OPEN_ECP_CONTEXT;

#endif /* (NTDDI_VERSION >= NTDDI_WIN7) */

#define PIN_WAIT                        (1)
#define PIN_EXCLUSIVE                   (2)
#define PIN_NO_READ                     (4)
#define PIN_IF_BCB                      (8)
#define PIN_CALLER_TRACKS_DIRTY_DATA    (32)
#define PIN_HIGH_PRIORITY               (64)

#define MAP_WAIT                        1
#define MAP_NO_READ                     (16)
#define MAP_HIGH_PRIORITY               (64)

#define IOCTL_REDIR_QUERY_PATH          CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 99, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_REDIR_QUERY_PATH_EX       CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 100, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef struct _QUERY_PATH_REQUEST {
  ULONG PathNameLength;
  PIO_SECURITY_CONTEXT SecurityContext;
  WCHAR FilePathName[1];
} QUERY_PATH_REQUEST, *PQUERY_PATH_REQUEST;

#if (NTDDI_VERSION >= NTDDI_VISTA)
typedef struct _QUERY_PATH_REQUEST_EX {
  PIO_SECURITY_CONTEXT pSecurityContext;
  ULONG EaLength;
  PVOID pEaBuffer;
  UNICODE_STRING PathName;
  UNICODE_STRING DomainServiceName;
  PECP_LIST EcpList;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS1)
  PESILO Silo;
  ULONG_PTR Reserved;
#else
  ULONG_PTR Reserved[2];
#endif
} QUERY_PATH_REQUEST_EX, *PQUERY_PATH_REQUEST_EX;
#endif

typedef struct _QUERY_PATH_RESPONSE {
  ULONG LengthAccepted;
} QUERY_PATH_RESPONSE, *PQUERY_PATH_RESPONSE;

#define VOLSNAPCONTROLTYPE                              0x00000053
#define IOCTL_VOLSNAP_FLUSH_AND_HOLD_WRITES             CTL_CODE(VOLSNAPCONTROLTYPE, 0, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_VOLSNAP_RELEASE_WRITES                    CTL_CODE(VOLSNAPCONTROLTYPE, 1, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

/* FIXME : These definitions below don't belong here (or anywhere in ddk really) */
#ifndef VER_PRODUCTBUILD
#define VER_PRODUCTBUILD 10000
#endif

#include "csq.h"

#define FS_LFN_APIS                             0x00004000

#define FILE_STORAGE_TYPE_SPECIFIED             0x00000041  /* FILE_DIRECTORY_FILE | FILE_NON_DIRECTORY_FILE */
#define FILE_STORAGE_TYPE_DEFAULT               (StorageTypeDefault << FILE_STORAGE_TYPE_SHIFT)
#define FILE_STORAGE_TYPE_DIRECTORY             (StorageTypeDirectory << FILE_STORAGE_TYPE_SHIFT)
#define FILE_STORAGE_TYPE_FILE                  (StorageTypeFile << FILE_STORAGE_TYPE_SHIFT)
#define FILE_STORAGE_TYPE_DOCFILE               (StorageTypeDocfile << FILE_STORAGE_TYPE_SHIFT)
#define FILE_STORAGE_TYPE_JUNCTION_POINT        (StorageTypeJunctionPoint << FILE_STORAGE_TYPE_SHIFT)
#define FILE_STORAGE_TYPE_CATALOG               (StorageTypeCatalog << FILE_STORAGE_TYPE_SHIFT)
#define FILE_STORAGE_TYPE_STRUCTURED_STORAGE    (StorageTypeStructuredStorage << FILE_STORAGE_TYPE_SHIFT)
#define FILE_STORAGE_TYPE_EMBEDDING             (StorageTypeEmbedding << FILE_STORAGE_TYPE_SHIFT)
#define FILE_STORAGE_TYPE_STREAM                (StorageTypeStream << FILE_STORAGE_TYPE_SHIFT)
#define FILE_MINIMUM_STORAGE_TYPE               FILE_STORAGE_TYPE_DEFAULT
#define FILE_MAXIMUM_STORAGE_TYPE               FILE_STORAGE_TYPE_STREAM
#define FILE_STORAGE_TYPE_MASK                  0x000f0000
#define FILE_STORAGE_TYPE_SHIFT                 16

#define FILE_VC_QUOTAS_LOG_VIOLATIONS           0x00000004

#ifdef _X86_
#define HARDWARE_PTE    HARDWARE_PTE_X86
#define PHARDWARE_PTE   PHARDWARE_PTE_X86
#endif

#define IO_ATTACH_DEVICE_API            0x80000000

#define IO_TYPE_APC                     18
#define IO_TYPE_DPC                     19
#define IO_TYPE_DEVICE_QUEUE            20
#define IO_TYPE_EVENT_PAIR              21
#define IO_TYPE_INTERRUPT               22
#define IO_TYPE_PROFILE                 23

#define IRP_BEING_VERIFIED              0x10

#define MAILSLOT_CLASS_FIRSTCLASS       1
#define MAILSLOT_CLASS_SECONDCLASS      2

#define MAILSLOT_SIZE_AUTO              0

#define MEM_DOS_LIM                     0x40000000

#define OB_TYPE_TYPE                    1
#define OB_TYPE_DIRECTORY               2
#define OB_TYPE_SYMBOLIC_LINK           3
#define OB_TYPE_TOKEN                   4
#define OB_TYPE_PROCESS                 5
#define OB_TYPE_THREAD                  6
#define OB_TYPE_EVENT                   7
#define OB_TYPE_EVENT_PAIR              8
#define OB_TYPE_MUTANT                  9
#define OB_TYPE_SEMAPHORE               10
#define OB_TYPE_TIMER                   11
#define OB_TYPE_PROFILE                 12
#define OB_TYPE_WINDOW_STATION          13
#define OB_TYPE_DESKTOP                 14
#define OB_TYPE_SECTION                 15
#define OB_TYPE_KEY                     16
#define OB_TYPE_PORT                    17
#define OB_TYPE_ADAPTER                 18
#define OB_TYPE_CONTROLLER              19
#define OB_TYPE_DEVICE                  20
#define OB_TYPE_DRIVER                  21
#define OB_TYPE_IO_COMPLETION           22
#define OB_TYPE_FILE                    23

#define SEC_BASED 0x00200000

/* end winnt.h */

#if (VER_PRODUCTBUILD >= 1381)
#define FSCTL_GET_HFS_INFORMATION       CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 31, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif /* (VER_PRODUCTBUILD >= 1381) */

#if (VER_PRODUCTBUILD >= 2195)

#define FSCTL_READ_PROPERTY_DATA        CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 33, METHOD_NEITHER, FILE_ANY_ACCESS)
#define FSCTL_WRITE_PROPERTY_DATA       CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 34, METHOD_NEITHER, FILE_ANY_ACCESS)

#define FSCTL_DUMP_PROPERTY_DATA        CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 37,  METHOD_NEITHER, FILE_ANY_ACCESS)

#define FSCTL_HSM_MSG                   CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 66, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
#define FSCTL_NSS_CONTROL               CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 67, METHOD_BUFFERED, FILE_WRITE_DATA)
#define FSCTL_HSM_DATA                  CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 68, METHOD_NEITHER, FILE_READ_DATA | FILE_WRITE_DATA)
#define FSCTL_NSS_RCONTROL              CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 70, METHOD_BUFFERED, FILE_READ_DATA)
#endif /* (VER_PRODUCTBUILD >= 2195) */

#define FSCTL_NETWORK_SET_CONFIGURATION_INFO    CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 102, METHOD_IN_DIRECT, FILE_ANY_ACCESS)
#define FSCTL_NETWORK_GET_CONFIGURATION_INFO    CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 103, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
#define FSCTL_NETWORK_GET_CONNECTION_INFO       CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 104, METHOD_NEITHER, FILE_ANY_ACCESS)
#define FSCTL_NETWORK_ENUMERATE_CONNECTIONS     CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 105, METHOD_NEITHER, FILE_ANY_ACCESS)
#define FSCTL_NETWORK_DELETE_CONNECTION         CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 107, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSCTL_NETWORK_GET_STATISTICS            CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 116, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSCTL_NETWORK_SET_DOMAIN_NAME           CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 120, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSCTL_NETWORK_REMOTE_BOOT_INIT_SCRT     CTL_CODE(FILE_DEVICE_NETWORK_FILE_SYSTEM, 250, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef enum _FILE_STORAGE_TYPE {
    StorageTypeDefault = 1,
    StorageTypeDirectory,
    StorageTypeFile,
    StorageTypeJunctionPoint,
    StorageTypeCatalog,
    StorageTypeStructuredStorage,
    StorageTypeEmbedding,
    StorageTypeStream
} FILE_STORAGE_TYPE;

typedef struct _OBJECT_BASIC_INFORMATION
{
    ULONG Attributes;
    ACCESS_MASK GrantedAccess;
    ULONG HandleCount;
    ULONG PointerCount;
    ULONG PagedPoolCharge;
    ULONG NonPagedPoolCharge;
    ULONG Reserved[ 3 ];
    ULONG NameInfoSize;
    ULONG TypeInfoSize;
    ULONG SecurityDescriptorSize;
    LARGE_INTEGER CreationTime;
} OBJECT_BASIC_INFORMATION, *POBJECT_BASIC_INFORMATION;

typedef struct _FILE_COPY_ON_WRITE_INFORMATION {
    BOOLEAN ReplaceIfExists;
    HANDLE  RootDirectory;
    ULONG   FileNameLength;
    WCHAR   FileName[1];
} FILE_COPY_ON_WRITE_INFORMATION, *PFILE_COPY_ON_WRITE_INFORMATION;

typedef struct _FILE_FULL_DIRECTORY_INFORMATION {
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   EndOfFile;
    LARGE_INTEGER   AllocationSize;
    ULONG           FileAttributes;
    ULONG           FileNameLength;
    ULONG           EaSize;
    WCHAR           FileName[ANYSIZE_ARRAY];
} FILE_FULL_DIRECTORY_INFORMATION, *PFILE_FULL_DIRECTORY_INFORMATION;

/* raw internal file lock struct returned from FsRtlGetNextFileLock */
typedef struct _FILE_SHARED_LOCK_ENTRY {
    PVOID           Unknown1;
    PVOID           Unknown2;
    FILE_LOCK_INFO  FileLock;
} FILE_SHARED_LOCK_ENTRY, *PFILE_SHARED_LOCK_ENTRY;

/* raw internal file lock struct returned from FsRtlGetNextFileLock */
typedef struct _FILE_EXCLUSIVE_LOCK_ENTRY {
    LIST_ENTRY      ListEntry;
    PVOID           Unknown1;
    PVOID           Unknown2;
    FILE_LOCK_INFO  FileLock;
} FILE_EXCLUSIVE_LOCK_ENTRY, *PFILE_EXCLUSIVE_LOCK_ENTRY;

typedef struct _FILE_MAILSLOT_PEEK_BUFFER {
    ULONG ReadDataAvailable;
    ULONG NumberOfMessages;
    ULONG MessageLength;
} FILE_MAILSLOT_PEEK_BUFFER, *PFILE_MAILSLOT_PEEK_BUFFER;

typedef struct _FILE_OLE_CLASSID_INFORMATION {
    GUID ClassId;
} FILE_OLE_CLASSID_INFORMATION, *PFILE_OLE_CLASSID_INFORMATION;

typedef struct _FILE_OLE_ALL_INFORMATION {
    FILE_BASIC_INFORMATION          BasicInformation;
    FILE_STANDARD_INFORMATION       StandardInformation;
    FILE_INTERNAL_INFORMATION       InternalInformation;
    FILE_EA_INFORMATION             EaInformation;
    FILE_ACCESS_INFORMATION         AccessInformation;
    FILE_POSITION_INFORMATION       PositionInformation;
    FILE_MODE_INFORMATION           ModeInformation;
    FILE_ALIGNMENT_INFORMATION      AlignmentInformation;
    USN                             LastChangeUsn;
    USN                             ReplicationUsn;
    LARGE_INTEGER                   SecurityChangeTime;
    FILE_OLE_CLASSID_INFORMATION    OleClassIdInformation;
    FILE_OBJECTID_INFORMATION       ObjectIdInformation;
    FILE_STORAGE_TYPE               StorageType;
    ULONG                           OleStateBits;
    ULONG                           OleId;
    ULONG                           NumberOfStreamReferences;
    ULONG                           StreamIndex;
    ULONG                           SecurityId;
    BOOLEAN                         ContentIndexDisable;
    BOOLEAN                         InheritContentIndexDisable;
    FILE_NAME_INFORMATION           NameInformation;
} FILE_OLE_ALL_INFORMATION, *PFILE_OLE_ALL_INFORMATION;

typedef struct _FILE_OLE_DIR_INFORMATION {
    ULONG               NextEntryOffset;
    ULONG               FileIndex;
    LARGE_INTEGER       CreationTime;
    LARGE_INTEGER       LastAccessTime;
    LARGE_INTEGER       LastWriteTime;
    LARGE_INTEGER       ChangeTime;
    LARGE_INTEGER       EndOfFile;
    LARGE_INTEGER       AllocationSize;
    ULONG               FileAttributes;
    ULONG               FileNameLength;
    FILE_STORAGE_TYPE   StorageType;
    GUID                OleClassId;
    ULONG               OleStateBits;
    BOOLEAN             ContentIndexDisable;
    BOOLEAN             InheritContentIndexDisable;
    WCHAR               FileName[1];
} FILE_OLE_DIR_INFORMATION, *PFILE_OLE_DIR_INFORMATION;

typedef struct _FILE_OLE_INFORMATION {
    LARGE_INTEGER                   SecurityChangeTime;
    FILE_OLE_CLASSID_INFORMATION    OleClassIdInformation;
    FILE_OBJECTID_INFORMATION       ObjectIdInformation;
    FILE_STORAGE_TYPE               StorageType;
    ULONG                           OleStateBits;
    BOOLEAN                         ContentIndexDisable;
    BOOLEAN                         InheritContentIndexDisable;
} FILE_OLE_INFORMATION, *PFILE_OLE_INFORMATION;

typedef struct _FILE_OLE_STATE_BITS_INFORMATION {
    ULONG StateBits;
    ULONG StateBitsMask;
} FILE_OLE_STATE_BITS_INFORMATION, *PFILE_OLE_STATE_BITS_INFORMATION;

typedef struct _MAPPING_PAIR {
    ULONGLONG Vcn;
    ULONGLONG Lcn;
} MAPPING_PAIR, *PMAPPING_PAIR;

typedef struct _GET_RETRIEVAL_DESCRIPTOR {
    ULONG           NumberOfPairs;
    ULONGLONG       StartVcn;
    MAPPING_PAIR    Pair[1];
} GET_RETRIEVAL_DESCRIPTOR, *PGET_RETRIEVAL_DESCRIPTOR;

typedef struct _MOVEFILE_DESCRIPTOR {
     HANDLE         FileHandle;
     ULONG          Reserved;
     LARGE_INTEGER  StartVcn;
     LARGE_INTEGER  TargetLcn;
     ULONG          NumVcns;
     ULONG          Reserved1;
} MOVEFILE_DESCRIPTOR, *PMOVEFILE_DESCRIPTOR;

typedef struct _OBJECT_BASIC_INFO {
    ULONG           Attributes;
    ACCESS_MASK     GrantedAccess;
    ULONG           HandleCount;
    ULONG           ReferenceCount;
    ULONG           PagedPoolUsage;
    ULONG           NonPagedPoolUsage;
    ULONG           Reserved[3];
    ULONG           NameInformationLength;
    ULONG           TypeInformationLength;
    ULONG           SecurityDescriptorLength;
    LARGE_INTEGER   CreateTime;
} OBJECT_BASIC_INFO, *POBJECT_BASIC_INFO;

typedef struct _OBJECT_HANDLE_ATTRIBUTE_INFO {
    BOOLEAN Inherit;
    BOOLEAN ProtectFromClose;
} OBJECT_HANDLE_ATTRIBUTE_INFO, *POBJECT_HANDLE_ATTRIBUTE_INFO;

typedef struct _OBJECT_NAME_INFO {
    UNICODE_STRING  ObjectName;
    WCHAR           ObjectNameBuffer[1];
} OBJECT_NAME_INFO, *POBJECT_NAME_INFO;

typedef struct _OBJECT_PROTECTION_INFO {
    BOOLEAN Inherit;
    BOOLEAN ProtectHandle;
} OBJECT_PROTECTION_INFO, *POBJECT_PROTECTION_INFO;

typedef struct _OBJECT_TYPE_INFO {
    UNICODE_STRING  ObjectTypeName;
    UCHAR           Unknown[0x58];
    WCHAR           ObjectTypeNameBuffer[1];
} OBJECT_TYPE_INFO, *POBJECT_TYPE_INFO;

typedef struct _OBJECT_ALL_TYPES_INFO {
    ULONG               NumberOfObjectTypes;
    OBJECT_TYPE_INFO    ObjectsTypeInfo[1];
} OBJECT_ALL_TYPES_INFO, *POBJECT_ALL_TYPES_INFO;

#if defined(USE_LPC6432)
#define LPC_CLIENT_ID CLIENT_ID64
#define LPC_SIZE_T ULONGLONG
#define LPC_PVOID ULONGLONG
#define LPC_HANDLE ULONGLONG
#else
#define LPC_CLIENT_ID CLIENT_ID
#define LPC_SIZE_T SIZE_T
#define LPC_PVOID PVOID
#define LPC_HANDLE HANDLE
#endif

typedef struct _PORT_MESSAGE
{
    union
    {
        struct
        {
            CSHORT DataLength;
            CSHORT TotalLength;
        } s1;
        ULONG Length;
    } u1;
    union
    {
        struct
        {
            CSHORT Type;
            CSHORT DataInfoOffset;
        } s2;
        ULONG ZeroInit;
    } u2;
    __GNU_EXTENSION union
    {
        LPC_CLIENT_ID ClientId;
        double DoNotUseThisField;
    };
    ULONG MessageId;
    __GNU_EXTENSION union
    {
        LPC_SIZE_T ClientViewSize;
        ULONG CallbackId;
    };
} PORT_MESSAGE, *PPORT_MESSAGE;

#define LPC_KERNELMODE_MESSAGE      (CSHORT)((USHORT)0x8000)

typedef struct _PORT_VIEW
{
    ULONG Length;
    LPC_HANDLE SectionHandle;
    ULONG SectionOffset;
    LPC_SIZE_T ViewSize;
    LPC_PVOID ViewBase;
    LPC_PVOID ViewRemoteBase;
} PORT_VIEW, *PPORT_VIEW;

typedef struct _REMOTE_PORT_VIEW
{
    ULONG Length;
    LPC_SIZE_T ViewSize;
    LPC_PVOID ViewBase;
} REMOTE_PORT_VIEW, *PREMOTE_PORT_VIEW;

typedef struct _VAD_HEADER {
    PVOID       StartVPN;
    PVOID       EndVPN;
    struct _VAD_HEADER* ParentLink;
    struct _VAD_HEADER* LeftLink;
    struct _VAD_HEADER* RightLink;
    ULONG       Flags;          /* LSB = CommitCharge */
    PVOID       ControlArea;
    PVOID       FirstProtoPte;
    PVOID       LastPTE;
    ULONG       Unknown;
    LIST_ENTRY  Secured;
} VAD_HEADER, *PVAD_HEADER;

NTKERNELAPI
LARGE_INTEGER
NTAPI
CcGetLsnForFileObject (
    _In_ PFILE_OBJECT FileObject,
    _Out_opt_ PLARGE_INTEGER OldestLsn
);

NTKERNELAPI
PVOID
NTAPI
FsRtlAllocatePool (
    _In_ POOL_TYPE PoolType,
    _In_ ULONG NumberOfBytes
);

NTKERNELAPI
PVOID
NTAPI
FsRtlAllocatePoolWithQuota (
    _In_ POOL_TYPE    PoolType,
    _In_ ULONG        NumberOfBytes
);

NTKERNELAPI
PVOID
NTAPI
FsRtlAllocatePoolWithQuotaTag (
    _In_ POOL_TYPE    PoolType,
    _In_ ULONG        NumberOfBytes,
    _In_ ULONG        Tag
);

NTKERNELAPI
PVOID
NTAPI
FsRtlAllocatePoolWithTag (
    _In_ POOL_TYPE    PoolType,
    _In_ ULONG        NumberOfBytes,
    _In_ ULONG        Tag
);

NTKERNELAPI
BOOLEAN
NTAPI
FsRtlMdlReadComplete (
    _In_ PFILE_OBJECT     FileObject,
    _In_ PMDL             MdlChain
);

NTKERNELAPI
BOOLEAN
NTAPI
FsRtlMdlWriteComplete (
    _In_ PFILE_OBJECT     FileObject,
    _In_ PLARGE_INTEGER   FileOffset,
    _In_ PMDL             MdlChain
);

NTKERNELAPI
VOID
NTAPI
FsRtlNotifyChangeDirectory (
    _In_ PNOTIFY_SYNC NotifySync,
    _In_ PVOID        FsContext,
    _In_ PSTRING      FullDirectoryName,
    _In_ PLIST_ENTRY  NotifyList,
    _In_ BOOLEAN      WatchTree,
    _In_ ULONG        CompletionFilter,
    _In_ PIRP         NotifyIrp
);

#if 1
NTKERNELAPI
NTSTATUS
NTAPI
ObCreateObject(
    _In_opt_ KPROCESSOR_MODE ObjectAttributesAccessMode,
    _In_ POBJECT_TYPE ObjectType,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_opt_ PVOID ParseContext,
    _In_ ULONG ObjectSize,
    _In_opt_ ULONG PagedPoolCharge,
    _In_opt_ ULONG NonPagedPoolCharge,
    _Out_ PVOID *Object
);

NTKERNELAPI
NTSTATUS
NTAPI
ObReferenceObjectByName (
    _In_ PUNICODE_STRING ObjectName,
    _In_ ULONG Attributes,
    _In_opt_ PACCESS_STATE PassedAccessState,
    _In_opt_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_opt_ PVOID ParseContext,
    _Out_ PVOID *Object
);

#define PsDereferenceImpersonationToken(T)  \
            {if (ARGUMENT_PRESENT(T)) {     \
                (ObDereferenceObject((T))); \
            } else {                        \
                ;                           \
            }                               \
}

NTKERNELAPI
NTSTATUS
NTAPI
PsLookupProcessThreadByCid (
    _In_ PCLIENT_ID   Cid,
    _Out_opt_ PEPROCESS   *Process,
    _Out_ PETHREAD    *Thread
);

NTSYSAPI
NTSTATUS
NTAPI
RtlSetSaclSecurityDescriptor (
    _Inout_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ BOOLEAN                  SaclPresent,
    _In_ PACL                     Sacl,
    _In_ BOOLEAN                  SaclDefaulted
);

#define SeEnableAccessToExports() SeExports = *(PSE_EXPORTS *)SeExports;

#endif

#define FILE_INVALID_FILE_ID               ((LONGLONG)-1LL)

typedef struct _FILE_ID_128 {
    UCHAR Identifier[16];
} FILE_ID_128, *PFILE_ID_128;

typedef struct _FILE_ID_INFORMATION {
    ULONGLONG VolumeSerialNumber;
    FILE_ID_128 FileId;
} FILE_ID_INFORMATION, *PFILE_ID_INFORMATION;

#define FILE_CS_FLAG_CASE_SENSITIVE_DIR     0x00000001

typedef struct _FILE_CASE_SENSITIVE_INFORMATION {
    ULONG Flags;
} FILE_CASE_SENSITIVE_INFORMATION, *PFILE_CASE_SENSITIVE_INFORMATION;

typedef struct _FILE_STAT_INFORMATION {
    LARGE_INTEGER FileId;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG FileAttributes;
    ULONG ReparseTag;
    ULONG NumberOfLinks;
    ACCESS_MASK EffectiveAccess;
} FILE_STAT_INFORMATION, *PFILE_STAT_INFORMATION;

#define LX_FILE_METADATA_HAS_UID 0x1
#define LX_FILE_METADATA_HAS_GID 0x2
#define LX_FILE_METADATA_HAS_MODE 0x4
#define LX_FILE_METADATA_HAS_DEVICE_ID 0x8
#define LX_FILE_CASE_SENSITIVE_DIR 0x10

typedef struct _FILE_STAT_LX_INFORMATION {
    LARGE_INTEGER FileId;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    ULONG FileAttributes;
    ULONG ReparseTag;
    ULONG NumberOfLinks;
    ACCESS_MASK EffectiveAccess;
    ULONG LxFlags;
    ULONG LxUid;
    ULONG LxGid;
    ULONG LxMode;
    ULONG LxDeviceIdMajor;
    ULONG LxDeviceIdMinor;
} FILE_STAT_LX_INFORMATION, *PFILE_STAT_LX_INFORMATION;

#define LX_FILE_METADATA_UID_EA_NAME "$LXUID"
#define LX_FILE_METADATA_GID_EA_NAME "$LXGID"
#define LX_FILE_METADATA_MODE_EA_NAME "$LXMOD"
#define LX_FILE_METADATA_DEVICE_ID_EA_NAME "$LXDEV"

#if ((NTDDI_VERSION >= NTDDI_WIN10_RS1))

#define FILE_RENAME_REPLACE_IF_EXISTS                     0x00000001
#define FILE_RENAME_POSIX_SEMANTICS                       0x00000002
#define FILE_RENAME_SUPPRESS_PIN_STATE_INHERITANCE        0x00000004
#define FILE_RENAME_SUPPRESS_STORAGE_RESERVE_INHERITANCE  0x00000008
#define FILE_RENAME_NO_INCREASE_AVAILABLE_SPACE           0x00000010
#define FILE_RENAME_NO_DECREASE_AVAILABLE_SPACE           0x00000020
#define FILE_RENAME_PRESERVE_AVAILABLE_SPACE              0x00000030
#define FILE_RENAME_IGNORE_READONLY_ATTRIBUTE             0x00000040
#define FILE_RENAME_FORCE_RESIZE_TARGET_SR                0x00000080
#define FILE_RENAME_FORCE_RESIZE_SOURCE_SR                0x00000100
#define FILE_RENAME_FORCE_RESIZE_SR                       0x00000180

#endif
#if ((_WIN32_WINNT >= _WIN32_WINNT_WIN7))

typedef enum _CSV_CONTROL_OP {
    CsvControlStartRedirectFile                  = 0x02,
    CsvControlStopRedirectFile                   = 0x03,
    CsvControlQueryRedirectState                 = 0x04,
    CsvControlQueryFileRevision                  = 0x06,
    CsvControlQueryMdsPath                       = 0x08,
    CsvControlQueryFileRevisionFileId128         = 0x09,
    CsvControlQueryVolumeRedirectState           = 0x0a,
    CsvControlEnableUSNRangeModificationTracking = 0x0d,
    CsvControlMarkHandleLocalVolumeMount         = 0x0e,
    CsvControlUnmarkHandleLocalVolumeMount       = 0x0f,
    CsvControlGetCsvFsMdsPathV2                  = 0x12,
    CsvControlDisableCaching                     = 0x13,
    CsvControlEnableCaching                      = 0x14,
    CsvControlStartForceDFO                      = 0x15,
    CsvControlStopForceDFO                       = 0x16,
    CsvControlQueryMdsPathNoPause                = 0x17,
    CsvControlSetVolumeId                        = 0x18,
    CsvControlQueryVolumeId                      = 0x19,
} CSV_CONTROL_OP, *PCSV_CONTROL_OP;

typedef struct _CSV_CONTROL_PARAM {
    CSV_CONTROL_OP Operation;
    LONGLONG Unused;
} CSV_CONTROL_PARAM, *PCSV_CONTROL_PARAM;

typedef struct _CSV_QUERY_FILE_REVISION {
    LONGLONG FileId;
    LONGLONG FileRevision[3];
} CSV_QUERY_FILE_REVISION, *PCSV_QUERY_FILE_REVISION;

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN8))

DEFINE_GUID( GUID_ECP_CSV_DOWN_LEVEL_OPEN,
             0x4248be44,
             0x647f,
             0x488f,
             0x8b, 0xe5, 0xa0, 0x8a, 0xaf, 0x70, 0xf0, 0x28);

DEFINE_GUID(GUID_ECP_CSV_QUERY_FILE_REVISION,
            0x44aec90b,
            0xde65,
            0x4d46,
            0x8f, 0xbf, 0x76, 0x3f, 0x9d, 0x97, 0xb, 0x1d);

typedef struct _CSV_QUERY_FILE_REVISION_ECP_CONTEXT {
    LONGLONG FileId;
    LONGLONG FileRevision[3];
} CSV_QUERY_FILE_REVISION_ECP_CONTEXT, *PCSV_QUERY_FILE_REVISION_ECP_CONTEXT;

#endif

#if (NTDDI_VERSION >= NTDDI_WIN10_VB)

typedef struct _COPY_INFORMATION {
    PFILE_OBJECT SourceFileObject;
    LONGLONG SourceFileOffset;
} COPY_INFORMATION, *PCOPY_INFORMATION;

NTSTATUS
NTAPI
IoGetCopyInformationExtension (
    _In_ PIRP Irp,
    _Out_ PCOPY_INFORMATION CopyInformation
    );

#endif

#define OPEN_REPARSE_POINT_VERSION_EX                                           (0x80000000)

#if (NTDDI_VERSION >= NTDDI_WIN10_RS4) || (defined(INCLUDE_OPEN_REPARSE_SUPPORT))
#define OPEN_REPARSE_POINT_REPARSE_IF_NON_FINAL_COMPONENT                       (0x00000016)
#define OPEN_REPARSE_POINT_REPARSE_IF_DIRECTORY_FINAL_COMPONENT_ALWAYS          (0x00000048)
#define OPEN_REPARSE_POINT_REPARSE_IF_NON_DIRECTORY_NON_FINAL_COMPONENT_ALWAYS  (0x00000050)
#define OPEN_REPARSE_POINT_REPARSE_IF_NON_DIRECTORY_FINAL_COMPONENT_ALWAYS      (0x00000060)
#define OPEN_REPARSE_POINT_REPARSE_IF_FINAL_COMPONENT                           (0x00000028)
#define OPEN_REPARSE_POINT_REPARSE_IF_FINAL_COMPONENT_ALWAYS                    (0x00000068)
#define OPEN_REPARSE_POINT_REPARSE_ALWAYS                                       (0x0000007E)
#endif

typedef struct _OPEN_REPARSE_LIST_ENTRY {
    LIST_ENTRY OpenReparseListEntry;
    ULONG      ReparseTag;
    ULONG      Flags;
    GUID       ReparseGuid;
    USHORT     Size;
    USHORT     RemainingLength;
} OPEN_REPARSE_LIST_ENTRY, *POPEN_REPARSE_LIST_ENTRY;

typedef struct _OPEN_REPARSE_LIST {
    LIST_ENTRY OpenReparseList;
} OPEN_REPARSE_LIST, *POPEN_REPARSE_LIST;

#ifdef __cplusplus
}
#endif

#ifndef ACCESS_REASON_EXDATA_MASK
#define ACCESS_REASON_EXDATA_MASK 0x7f000000
#endif
#ifndef ACCESS_REASON_STAGING_MASK
#define ACCESS_REASON_STAGING_MASK 0x80000000
#endif
#ifndef ASC_REQ_EXPLICIT_SESSION
#define ASC_REQ_EXPLICIT_SESSION 0x0000001000000000
#endif
#ifndef ASC_REQ_MESSAGES
#define ASC_REQ_MESSAGES 0x0000000100000000
#endif
#ifndef ASC_REQ_SESSION_TICKET
#define ASC_REQ_SESSION_TICKET 0x00000040
#endif
#ifndef ASC_RET_EXPLICIT_SESSION
#define ASC_RET_EXPLICIT_SESSION 0x0000001000000000
#endif
#ifndef ASC_RET_MESSAGES
#define ASC_RET_MESSAGES 0x0000000100000000
#endif
#ifndef ASC_RET_REUSE_SESSION_TICKETS
#define ASC_RET_REUSE_SESSION_TICKETS 0x0000000800000000
#endif
#ifndef ASC_RET_SESSION_TICKET
#define ASC_RET_SESSION_TICKET 0x00000040
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_BEST_EFFORT
#define ATOMIC_CREATE_ECP_IN_FLAG_BEST_EFFORT 0x0100
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_EOF_SPECIFIED
#define ATOMIC_CREATE_ECP_IN_FLAG_EOF_SPECIFIED 0x0004
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_FILE_ATTRIBUTES_SPECIFIED
#define ATOMIC_CREATE_ECP_IN_FLAG_FILE_ATTRIBUTES_SPECIFIED 0x0020
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_GEN_FLAGS_SPECIFIED
#define ATOMIC_CREATE_ECP_IN_FLAG_GEN_FLAGS_SPECIFIED 0x8000
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_MARK_USN_SOURCE_INFO
#define ATOMIC_CREATE_ECP_IN_FLAG_MARK_USN_SOURCE_INFO 0x0800
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_OPERATION_MASK
#define ATOMIC_CREATE_ECP_IN_FLAG_OPERATION_MASK 0x00ff
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_OP_FLAGS_SPECIFIED
#define ATOMIC_CREATE_ECP_IN_FLAG_OP_FLAGS_SPECIFIED 0x0080
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_REPARSE_POINT_SPECIFIED
#define ATOMIC_CREATE_ECP_IN_FLAG_REPARSE_POINT_SPECIFIED 0x0002
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_SPARSE_SPECIFIED
#define ATOMIC_CREATE_ECP_IN_FLAG_SPARSE_SPECIFIED 0x0001
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_SUPPRESS_DIR_CHANGE_NOTIFY
#define ATOMIC_CREATE_ECP_IN_FLAG_SUPPRESS_DIR_CHANGE_NOTIFY 0x0400
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_SUPPRESS_FILE_ATTRIBUTE_INHERITANCE
#define ATOMIC_CREATE_ECP_IN_FLAG_SUPPRESS_FILE_ATTRIBUTE_INHERITANCE 0x0040
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_SUPPRESS_PARENT_TIMESTAMPS_UPDATE
#define ATOMIC_CREATE_ECP_IN_FLAG_SUPPRESS_PARENT_TIMESTAMPS_UPDATE 0x0200
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_TIMESTAMPS_SPECIFIED
#define ATOMIC_CREATE_ECP_IN_FLAG_TIMESTAMPS_SPECIFIED 0x0010
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_VDL_SPECIFIED
#define ATOMIC_CREATE_ECP_IN_FLAG_VDL_SPECIFIED 0x0008
#endif
#ifndef ATOMIC_CREATE_ECP_IN_FLAG_WRITE_USN_CLOSE_RECORD
#define ATOMIC_CREATE_ECP_IN_FLAG_WRITE_USN_CLOSE_RECORD 0x1000
#endif
#ifndef ATOMIC_CREATE_ECP_IN_OP_FLAG_CASE_SENSITIVE_FLAGS_SPECIFIED
#define ATOMIC_CREATE_ECP_IN_OP_FLAG_CASE_SENSITIVE_FLAGS_SPECIFIED 0x00000001
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_EOF_SET
#define ATOMIC_CREATE_ECP_OUT_FLAG_EOF_SET 0x0004
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_FILE_ATTRIBUTES_RETURNED
#define ATOMIC_CREATE_ECP_OUT_FLAG_FILE_ATTRIBUTES_RETURNED 0x0200
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_FILE_ATTRIBUTES_SET
#define ATOMIC_CREATE_ECP_OUT_FLAG_FILE_ATTRIBUTES_SET 0x0020
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_FILE_ATTRIBUTE_INHERITANCE_SUPPRESSED
#define ATOMIC_CREATE_ECP_OUT_FLAG_FILE_ATTRIBUTE_INHERITANCE_SUPPRESSED 0x0040
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_OPERATION_MASK
#define ATOMIC_CREATE_ECP_OUT_FLAG_OPERATION_MASK 0x00ff
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_OP_FLAGS_HONORED
#define ATOMIC_CREATE_ECP_OUT_FLAG_OP_FLAGS_HONORED 0x0080
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_REPARSE_POINT_SET
#define ATOMIC_CREATE_ECP_OUT_FLAG_REPARSE_POINT_SET 0x0002
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_SPARSE_SET
#define ATOMIC_CREATE_ECP_OUT_FLAG_SPARSE_SET 0x0001
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_TIMESTAMPS_RETURNED
#define ATOMIC_CREATE_ECP_OUT_FLAG_TIMESTAMPS_RETURNED 0x0100
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_TIMESTAMPS_SET
#define ATOMIC_CREATE_ECP_OUT_FLAG_TIMESTAMPS_SET 0x0010
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_USN_CLOSE_RECORD_WRITTEN
#define ATOMIC_CREATE_ECP_OUT_FLAG_USN_CLOSE_RECORD_WRITTEN 0x0800
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_USN_RETURNED
#define ATOMIC_CREATE_ECP_OUT_FLAG_USN_RETURNED 0x1000
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_USN_SOURCE_INFO_MARKED
#define ATOMIC_CREATE_ECP_OUT_FLAG_USN_SOURCE_INFO_MARKED 0x0400
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_FLAG_VDL_SET
#define ATOMIC_CREATE_ECP_OUT_FLAG_VDL_SET 0x0008
#endif
#ifndef ATOMIC_CREATE_ECP_OUT_OP_FLAG_CASE_SENSITIVE_FLAGS_SET
#define ATOMIC_CREATE_ECP_OUT_OP_FLAG_CASE_SENSITIVE_FLAGS_SET 0x00000001
#endif
#ifndef CACHE_MANAGER_CALLBACKS_EX_V1
#define CACHE_MANAGER_CALLBACKS_EX_V1 1
#endif
#ifndef CACHE_USE_DIRECT_ACCESS_MAPPING
#define CACHE_USE_DIRECT_ACCESS_MAPPING 0x00000001
#endif
#ifndef CC_ACQUIRE_DONT_WAIT
#define CC_ACQUIRE_DONT_WAIT 0x00000001
#endif
#ifndef CC_ACQUIRE_SUPPORTS_ASYNC_LAZYWRITE
#define CC_ACQUIRE_SUPPORTS_ASYNC_LAZYWRITE 0x00000001
#endif
#ifndef CC_AGGRESSIVE_UNMAP_BEHIND
#define CC_AGGRESSIVE_UNMAP_BEHIND 0x00000001
#endif
#ifndef CC_DIRTY_PAGES_INFO_V1
#define CC_DIRTY_PAGES_INFO_V1 1
#endif
#ifndef CC_DISABLE_READ_AHEAD
#define CC_DISABLE_READ_AHEAD 0x00000002
#endif
#ifndef CC_DISABLE_WRITE_BEHIND
#define CC_DISABLE_WRITE_BEHIND 0x00000004
#endif
#ifndef CC_ENABLE_CPU_CACHE
#define CC_ENABLE_CPU_CACHE 0x10000000
#endif
#ifndef CC_FLUSH_AND_PURGE_GATHER_DIRTY_BITS
#define CC_FLUSH_AND_PURGE_GATHER_DIRTY_BITS 0x2
#endif
#ifndef CC_FLUSH_AND_PURGE_WRITEABLE_VIEWS_NOTSEEN
#define CC_FLUSH_AND_PURGE_WRITEABLE_VIEWS_NOTSEEN 0x4
#endif
#ifndef CHECKSUM_TYPE_CRC32
#define CHECKSUM_TYPE_CRC32 1
#endif
#ifndef CHECKSUM_TYPE_CRC64
#define CHECKSUM_TYPE_CRC64 2
#endif
#ifndef CHECKSUM_TYPE_ECC
#define CHECKSUM_TYPE_ECC 3
#endif
#ifndef CHECKSUM_TYPE_FIRST_UNUSED_TYPE
#define CHECKSUM_TYPE_FIRST_UNUSED_TYPE 6
#endif
#ifndef CHECKSUM_TYPE_NONE
#define CHECKSUM_TYPE_NONE 0
#endif
#ifndef CHECKSUM_TYPE_SHA256
#define CHECKSUM_TYPE_SHA256 4
#endif
#ifndef CHECKSUM_TYPE_XXH64
#define CHECKSUM_TYPE_XXH64 5
#endif
#ifndef COMPRESSION_ENGINE_MASK
#define COMPRESSION_ENGINE_MASK 0xFF00
#endif
#ifndef COMPRESSION_ENGINE_MAX
#define COMPRESSION_ENGINE_MAX 0x0200
#endif
#ifndef COMPRESSION_FORMAT_DEFLATE
#define COMPRESSION_FORMAT_DEFLATE 0x0007
#endif
#ifndef COMPRESSION_FORMAT_LZ4
#define COMPRESSION_FORMAT_LZ4 0x0006
#endif
#ifndef COMPRESSION_FORMAT_MASK
#define COMPRESSION_FORMAT_MASK 0x00FF
#endif
#ifndef COMPRESSION_FORMAT_MAX
#define COMPRESSION_FORMAT_MAX 0x0008
#endif
#ifndef COMPRESSION_FORMAT_XP10
#define COMPRESSION_FORMAT_XP10 0x0005
#endif
#ifndef COMPRESSION_FORMAT_XPRESS
#define COMPRESSION_FORMAT_XPRESS 0x0003
#endif
#ifndef COMPRESSION_FORMAT_XPRESS_HUFF
#define COMPRESSION_FORMAT_XPRESS_HUFF 0x0004
#endif
#ifndef COMPRESSION_FORMAT_ZLIB
#define COMPRESSION_FORMAT_ZLIB 0x0008
#endif
#ifndef CONTAINER_ROOT_INFO_FLAG_BIND_DO_NOT_MAP_NAME
#define CONTAINER_ROOT_INFO_FLAG_BIND_DO_NOT_MAP_NAME 0x00000100
#endif
#ifndef CONTAINER_ROOT_INFO_FLAG_BIND_EXCEPTION_ROOT
#define CONTAINER_ROOT_INFO_FLAG_BIND_EXCEPTION_ROOT 0x00000080
#endif
#ifndef CONTAINER_ROOT_INFO_FLAG_BIND_ROOT
#define CONTAINER_ROOT_INFO_FLAG_BIND_ROOT 0x00000020
#endif
#ifndef CONTAINER_ROOT_INFO_FLAG_BIND_TARGET_ROOT
#define CONTAINER_ROOT_INFO_FLAG_BIND_TARGET_ROOT 0x00000040
#endif
#ifndef CONTAINER_ROOT_INFO_FLAG_LAYER_ROOT
#define CONTAINER_ROOT_INFO_FLAG_LAYER_ROOT 0x00000002
#endif
#ifndef CONTAINER_ROOT_INFO_FLAG_SCRATCH_ROOT
#define CONTAINER_ROOT_INFO_FLAG_SCRATCH_ROOT 0x00000001
#endif
#ifndef CONTAINER_ROOT_INFO_FLAG_UNION_LAYER_ROOT
#define CONTAINER_ROOT_INFO_FLAG_UNION_LAYER_ROOT 0x00000200
#endif
#ifndef CONTAINER_ROOT_INFO_FLAG_VIRTUALIZATION_EXCEPTION_ROOT
#define CONTAINER_ROOT_INFO_FLAG_VIRTUALIZATION_EXCEPTION_ROOT 0x00000010
#endif
#ifndef CONTAINER_ROOT_INFO_FLAG_VIRTUALIZATION_ROOT
#define CONTAINER_ROOT_INFO_FLAG_VIRTUALIZATION_ROOT 0x00000004
#endif
#ifndef CONTAINER_ROOT_INFO_FLAG_VIRTUALIZATION_TARGET_ROOT
#define CONTAINER_ROOT_INFO_FLAG_VIRTUALIZATION_TARGET_ROOT 0x00000008
#endif
#ifndef CONTAINER_ROOT_INFO_VALID_FLAGS
#define CONTAINER_ROOT_INFO_VALID_FLAGS 0x000003ff
#endif
#ifndef CONTAINER_VOLUME_STATE_HOSTING_CONTAINER
#define CONTAINER_VOLUME_STATE_HOSTING_CONTAINER 0x00000001
#endif
#ifndef COPY_FILE_CHUNK_DUPLICATE_EXTENTS
#define COPY_FILE_CHUNK_DUPLICATE_EXTENTS 0x00000001L
#endif
#ifndef CREATE_REDIRECTION_FLAGS_SERVICED_FROM_LAYER
#define CREATE_REDIRECTION_FLAGS_SERVICED_FROM_LAYER 0x0001
#endif
#ifndef CREATE_REDIRECTION_FLAGS_SERVICED_FROM_REGISTERED_LAYER
#define CREATE_REDIRECTION_FLAGS_SERVICED_FROM_REGISTERED_LAYER 0x0004
#endif
#ifndef CREATE_REDIRECTION_FLAGS_SERVICED_FROM_REMOTE_LAYER
#define CREATE_REDIRECTION_FLAGS_SERVICED_FROM_REMOTE_LAYER 0x0008
#endif
#ifndef CREATE_REDIRECTION_FLAGS_SERVICED_FROM_SCRATCH
#define CREATE_REDIRECTION_FLAGS_SERVICED_FROM_SCRATCH 0x0002
#endif
#ifndef CREATE_REDIRECTION_FLAGS_SERVICED_FROM_USER_MODE
#define CREATE_REDIRECTION_FLAGS_SERVICED_FROM_USER_MODE 0x0010
#endif
#ifndef CRITICAL_ACE_FLAG
#define CRITICAL_ACE_FLAG 0x20
#endif
#ifndef CSV_MGMTLOCK_CHECK_VOLUME_REDIRECTED
#define CSV_MGMTLOCK_CHECK_VOLUME_REDIRECTED 0x00000001
#endif
#ifndef CSV_QUERY_MDS_PATH_FLAG_CSV_DIRECT_IO_ENABLED
#define CSV_QUERY_MDS_PATH_FLAG_CSV_DIRECT_IO_ENABLED 0x2
#endif
#ifndef CSV_QUERY_MDS_PATH_FLAG_SMB_BYPASS_CSV_ENABLED
#define CSV_QUERY_MDS_PATH_FLAG_SMB_BYPASS_CSV_ENABLED 0x4
#endif
#ifndef CSV_QUERY_MDS_PATH_FLAG_STORAGE_ON_THIS_NODE_IS_CONNECTED
#define CSV_QUERY_MDS_PATH_FLAG_STORAGE_ON_THIS_NODE_IS_CONNECTED 0x1
#endif
#ifndef CSV_QUERY_MDS_PATH_V2_VERSION_1
#define CSV_QUERY_MDS_PATH_V2_VERSION_1 1
#endif
#ifndef DAX_ALLOC_ALIGNMENT_FLAG_FALLBACK_SPECIFIED
#define DAX_ALLOC_ALIGNMENT_FLAG_FALLBACK_SPECIFIED 0x00000002
#endif
#ifndef DAX_ALLOC_ALIGNMENT_FLAG_MANDATORY
#define DAX_ALLOC_ALIGNMENT_FLAG_MANDATORY 0x00000001
#endif
#ifndef DEVICE_RESET_KEEP_STACK
#define DEVICE_RESET_KEEP_STACK 0x4
#endif
#ifndef DEVICE_RESET_RESERVED_0
#define DEVICE_RESET_RESERVED_0 0x1
#endif
#ifndef DEVICE_RESET_RESERVED_1
#define DEVICE_RESET_RESERVED_1 0x2
#endif
#ifndef DOMAIN_ALIAS_RID_ACCESS_CONTROL_ASSISTANCE_OPS
#define DOMAIN_ALIAS_RID_ACCESS_CONTROL_ASSISTANCE_OPS 0x00000243L
#endif
#ifndef DOMAIN_ALIAS_RID_DEFAULT_ACCOUNT
#define DOMAIN_ALIAS_RID_DEFAULT_ACCOUNT 0x00000245L
#endif
#ifndef DOMAIN_ALIAS_RID_DEVICE_OWNERS
#define DOMAIN_ALIAS_RID_DEVICE_OWNERS 0x00000247L
#endif
#ifndef DOMAIN_ALIAS_RID_HYPER_V_ADMINS
#define DOMAIN_ALIAS_RID_HYPER_V_ADMINS 0x00000242L
#endif
#ifndef DOMAIN_ALIAS_RID_OPENSSH_USERS
#define DOMAIN_ALIAS_RID_OPENSSH_USERS 0x00000249L
#endif
#ifndef DOMAIN_ALIAS_RID_RDS_ENDPOINT_SERVERS
#define DOMAIN_ALIAS_RID_RDS_ENDPOINT_SERVERS 0x00000240L
#endif
#ifndef DOMAIN_ALIAS_RID_RDS_MANAGEMENT_SERVERS
#define DOMAIN_ALIAS_RID_RDS_MANAGEMENT_SERVERS 0x00000241L
#endif
#ifndef DOMAIN_ALIAS_RID_RDS_REMOTE_ACCESS_SERVERS
#define DOMAIN_ALIAS_RID_RDS_REMOTE_ACCESS_SERVERS 0x0000023FL
#endif
#ifndef DOMAIN_ALIAS_RID_REMOTE_MANAGEMENT_USERS
#define DOMAIN_ALIAS_RID_REMOTE_MANAGEMENT_USERS 0x00000244L
#endif
#ifndef DOMAIN_ALIAS_RID_STORAGE_REPLICA_ADMINS
#define DOMAIN_ALIAS_RID_STORAGE_REPLICA_ADMINS 0x00000246L
#endif
#ifndef DOMAIN_ALIAS_RID_USER_MODE_HARDWARE_OPERATORS
#define DOMAIN_ALIAS_RID_USER_MODE_HARDWARE_OPERATORS 0x00000248L
#endif
#ifndef DOMAIN_GROUP_RID_AUTHORIZATION_DATA_CONTAINS_CLAIMS
#define DOMAIN_GROUP_RID_AUTHORIZATION_DATA_CONTAINS_CLAIMS 0x000001F1L
#endif
#ifndef DOMAIN_GROUP_RID_AUTHORIZATION_DATA_IS_COMPOUNDED
#define DOMAIN_GROUP_RID_AUTHORIZATION_DATA_IS_COMPOUNDED 0x000001F0L
#endif
#ifndef DOMAIN_GROUP_RID_CDC_RESERVED
#define DOMAIN_GROUP_RID_CDC_RESERVED 0x0000020CL
#endif
#ifndef DOMAIN_GROUP_RID_CLONEABLE_CONTROLLERS
#define DOMAIN_GROUP_RID_CLONEABLE_CONTROLLERS 0x0000020AL
#endif
#ifndef DOMAIN_GROUP_RID_ENTERPRISE_KEY_ADMINS
#define DOMAIN_GROUP_RID_ENTERPRISE_KEY_ADMINS 0x0000020FL
#endif
#ifndef DOMAIN_GROUP_RID_EXTERNAL_TRUSTS
#define DOMAIN_GROUP_RID_EXTERNAL_TRUSTS 0x00000211L
#endif
#ifndef DOMAIN_GROUP_RID_FOREST_TRUSTS
#define DOMAIN_GROUP_RID_FOREST_TRUSTS 0x00000210L
#endif
#ifndef DOMAIN_GROUP_RID_KEY_ADMINS
#define DOMAIN_GROUP_RID_KEY_ADMINS 0x0000020EL
#endif
#ifndef DOMAIN_GROUP_RID_PROTECTED_USERS
#define DOMAIN_GROUP_RID_PROTECTED_USERS 0x0000020DL
#endif
#ifndef DOMAIN_USER_RID_DEFAULT_ACCOUNT
#define DOMAIN_USER_RID_DEFAULT_ACCOUNT 0x000001F7L
#endif
#ifndef DOMAIN_USER_RID_WDAG_ACCOUNT
#define DOMAIN_USER_RID_WDAG_ACCOUNT 0x000001F8L
#endif
#ifndef DO_SUPPORTS_PERSISTENT_ACLS
#define DO_SUPPORTS_PERSISTENT_ACLS 0x00020000
#endif
#ifndef DUPLICATE_EXTENTS_DATA_EX_ASYNC
#define DUPLICATE_EXTENTS_DATA_EX_ASYNC 0x00000002
#endif
#ifndef DUPLICATE_EXTENTS_DATA_EX_SOURCE_ATOMIC
#define DUPLICATE_EXTENTS_DATA_EX_SOURCE_ATOMIC 0x00000001
#endif
#ifndef ECP_OPEN_PARAMETERS_FLAG_FAIL_ON_CASE_SENSITIVE_DIR
#define ECP_OPEN_PARAMETERS_FLAG_FAIL_ON_CASE_SENSITIVE_DIR 0x00000010
#endif
#ifndef ECP_OPEN_PARAMETERS_FLAG_IGNORE_DIR_CASE_SENSITIVITY
#define ECP_OPEN_PARAMETERS_FLAG_IGNORE_DIR_CASE_SENSITIVITY 0x00000008
#endif
#ifndef ECP_OPEN_PARAMETERS_FLAG_OPEN_FOR_DELETE
#define ECP_OPEN_PARAMETERS_FLAG_OPEN_FOR_DELETE 0x00000004
#endif
#ifndef ECP_OPEN_PARAMETERS_FLAG_OPEN_FOR_READ
#define ECP_OPEN_PARAMETERS_FLAG_OPEN_FOR_READ 0x00000001
#endif
#ifndef ECP_OPEN_PARAMETERS_FLAG_OPEN_FOR_WRITE
#define ECP_OPEN_PARAMETERS_FLAG_OPEN_FOR_WRITE 0x00000002
#endif
#ifndef EFS_TRACKED_OFFSET_HEADER_FLAG
#define EFS_TRACKED_OFFSET_HEADER_FLAG 0x0001
#endif
#ifndef ENCRYPTED_DATA_INFO_4K_SPARSE_UNIT
#define ENCRYPTED_DATA_INFO_4K_SPARSE_UNIT 0x00000004
#endif
#ifndef ENCRYPTED_DATA_INFO_SPARSE_DATA
#define ENCRYPTED_DATA_INFO_SPARSE_DATA 0x00000002
#endif
#ifndef FILESYSTEM_STATISTICS_TYPE_REFS
#define FILESYSTEM_STATISTICS_TYPE_REFS 4
#endif
#ifndef FILE_CLEANUP_FILE_DELETED
#define FILE_CLEANUP_FILE_DELETED 0x00000004
#endif
#ifndef FILE_CLEANUP_FILE_REMAINS
#define FILE_CLEANUP_FILE_REMAINS 0x00000002
#endif
#ifndef FILE_CLEANUP_LINK_DELETED
#define FILE_CLEANUP_LINK_DELETED 0x00000008
#endif
#ifndef FILE_CLEANUP_POSIX_STYLE_DELETE
#define FILE_CLEANUP_POSIX_STYLE_DELETE 0x00000020
#endif
#ifndef FILE_CLEANUP_STREAM_DELETED
#define FILE_CLEANUP_STREAM_DELETED 0x00000010
#endif
#ifndef FILE_CLEANUP_UNKNOWN
#define FILE_CLEANUP_UNKNOWN 0x00000000
#endif
#ifndef FILE_CLEANUP_WRONG_DEVICE
#define FILE_CLEANUP_WRONG_DEVICE 0x00000001
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_ATTRIBUTE_NON_RESIDENT
#define FILE_INITIATE_REPAIR_HINT1_ATTRIBUTE_NON_RESIDENT 0x0000002000000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_ATTRIBUTE_NOT_FOUND
#define FILE_INITIATE_REPAIR_HINT1_ATTRIBUTE_NOT_FOUND 0x0000000000001000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_ATTRIBUTE_TOO_SMALL
#define FILE_INITIATE_REPAIR_HINT1_ATTRIBUTE_TOO_SMALL 0x0000001000000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_CLUSTERS_ALREADY_IN_USE
#define FILE_INITIATE_REPAIR_HINT1_CLUSTERS_ALREADY_IN_USE 0x0000000000008000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_DENY_DEFRAG
#define FILE_INITIATE_REPAIR_HINT1_DENY_DEFRAG 0x0000004000000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_IS_BASE_RECORD
#define FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_IS_BASE_RECORD 0x0000000000080000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_NOT_BASE_RECORD
#define FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_NOT_BASE_RECORD 0x0000000000000008
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_NOT_EXIST
#define FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_NOT_EXIST 0x0000000000000004
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_NOT_IN_USE
#define FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_NOT_IN_USE 0x0000000000000001
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_NOT_ORPHAN
#define FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_NOT_ORPHAN 0x0000000000040000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_REUSED
#define FILE_INITIATE_REPAIR_HINT1_FILE_RECORD_REUSED 0x0000000000000002
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_INDEX_ENTRY_MISMATCH
#define FILE_INITIATE_REPAIR_HINT1_INDEX_ENTRY_MISMATCH 0x0000010000000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_INVALID_ARRAY_LENGTH_COUNT
#define FILE_INITIATE_REPAIR_HINT1_INVALID_ARRAY_LENGTH_COUNT 0x0000000000100000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_INVALID_LCN
#define FILE_INITIATE_REPAIR_HINT1_INVALID_LCN 0x0000000100000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_INVALID_ORPHAN_RECOVERY_NAME
#define FILE_INITIATE_REPAIR_HINT1_INVALID_ORPHAN_RECOVERY_NAME 0x0000020000000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_INVALID_PARENT
#define FILE_INITIATE_REPAIR_HINT1_INVALID_PARENT 0x0000000000800000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_INVALID_RUN_LENGTH
#define FILE_INITIATE_REPAIR_HINT1_INVALID_RUN_LENGTH 0x0000000000020000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_INVALID_VCN
#define FILE_INITIATE_REPAIR_HINT1_INVALID_VCN 0x0000000200000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_LCN_NOT_EXIST
#define FILE_INITIATE_REPAIR_HINT1_LCN_NOT_EXIST 0x0000000000010000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_MULTIPLE_FILE_NAME_ATTRIBUTES
#define FILE_INITIATE_REPAIR_HINT1_MULTIPLE_FILE_NAME_ATTRIBUTES 0x0000040000000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_NAME_CONFLICT
#define FILE_INITIATE_REPAIR_HINT1_NAME_CONFLICT 0x0000000400000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_NOTHING_WRONG
#define FILE_INITIATE_REPAIR_HINT1_NOTHING_WRONG 0x0000000000000800
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_NOT_IMPLEMENTED
#define FILE_INITIATE_REPAIR_HINT1_NOT_IMPLEMENTED 0x0000000000000020
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_ORPHAN
#define FILE_INITIATE_REPAIR_HINT1_ORPHAN 0x0000000800000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_ORPHAN_GENERATED
#define FILE_INITIATE_REPAIR_HINT1_ORPHAN_GENERATED 0x0000000000000200
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_OUT_OF_GENERIC_NAMES
#define FILE_INITIATE_REPAIR_HINT1_OUT_OF_GENERIC_NAMES 0x0000000040000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_OUT_OF_RESOURCE
#define FILE_INITIATE_REPAIR_HINT1_OUT_OF_RESOURCE 0x0000000080000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_PARENT_FILE_RECORD_NOT_BASE_RECORD
#define FILE_INITIATE_REPAIR_HINT1_PARENT_FILE_RECORD_NOT_BASE_RECORD 0x0000000008000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_PARENT_FILE_RECORD_NOT_EXIST
#define FILE_INITIATE_REPAIR_HINT1_PARENT_FILE_RECORD_NOT_EXIST 0x0000000004000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_PARENT_FILE_RECORD_NOT_INDEX
#define FILE_INITIATE_REPAIR_HINT1_PARENT_FILE_RECORD_NOT_INDEX 0x0000000010000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_PARENT_FILE_RECORD_NOT_IN_USE
#define FILE_INITIATE_REPAIR_HINT1_PARENT_FILE_RECORD_NOT_IN_USE 0x0000000001000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_PARENT_FILE_RECORD_REUSED
#define FILE_INITIATE_REPAIR_HINT1_PARENT_FILE_RECORD_REUSED 0x0000000002000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_POTENTIAL_CROSSLINK
#define FILE_INITIATE_REPAIR_HINT1_POTENTIAL_CROSSLINK 0x0000000000002000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_PREVIOUS_PARENT_STILL_VALID
#define FILE_INITIATE_REPAIR_HINT1_PREVIOUS_PARENT_STILL_VALID 0x0000008000000000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_RECURSIVELY_CORRUPTED
#define FILE_INITIATE_REPAIR_HINT1_RECURSIVELY_CORRUPTED 0x0000000000000100
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_REPAIRED
#define FILE_INITIATE_REPAIR_HINT1_REPAIRED 0x0000000000000400
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_REPAIR_DISABLED
#define FILE_INITIATE_REPAIR_HINT1_REPAIR_DISABLED 0x0000000000000080
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_SID_MISMATCH
#define FILE_INITIATE_REPAIR_HINT1_SID_MISMATCH 0x0000000000400000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_SID_VALID
#define FILE_INITIATE_REPAIR_HINT1_SID_VALID 0x0000000000200000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_STALE_INFORMATION
#define FILE_INITIATE_REPAIR_HINT1_STALE_INFORMATION 0x0000000000004000
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_SYSTEM_FILE
#define FILE_INITIATE_REPAIR_HINT1_SYSTEM_FILE 0x0000000000000010
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_UNABLE_TO_REPAIR
#define FILE_INITIATE_REPAIR_HINT1_UNABLE_TO_REPAIR 0x0000000000000040
#endif
#ifndef FILE_INITIATE_REPAIR_HINT1_VALID_INDEX_ENTRY
#define FILE_INITIATE_REPAIR_HINT1_VALID_INDEX_ENTRY 0x0000000020000000
#endif
#ifndef FILE_LAYOUT_NAME_ENTRY_DOS
#define FILE_LAYOUT_NAME_ENTRY_DOS 0x00000002
#endif
#ifndef FILE_LAYOUT_NAME_ENTRY_PRIMARY
#define FILE_LAYOUT_NAME_ENTRY_PRIMARY 0x00000001
#endif
#ifndef FILE_NAME_FLAGS_UNSPECIFIED
#define FILE_NAME_FLAGS_UNSPECIFIED 0x80
#endif
#ifndef FILE_NAME_FLAG_BOTH
#define FILE_NAME_FLAG_BOTH 0x03
#endif
#ifndef FILE_NAME_FLAG_DOS
#define FILE_NAME_FLAG_DOS 0x02
#endif
#ifndef FILE_NAME_FLAG_HARDLINK
#define FILE_NAME_FLAG_HARDLINK 0
#endif
#ifndef FILE_NAME_FLAG_NTFS
#define FILE_NAME_FLAG_NTFS 0x01
#endif
#ifndef FILE_PIPE_SYMLINK_FLAG_GLOBAL
#define FILE_PIPE_SYMLINK_FLAG_GLOBAL 0x1
#endif
#ifndef FILE_PIPE_SYMLINK_FLAG_RELATIVE
#define FILE_PIPE_SYMLINK_FLAG_RELATIVE 0x2
#endif
#ifndef FILE_PROVIDER_COMPRESSION_LZX
#define FILE_PROVIDER_COMPRESSION_LZX 0x00000001
#endif
#ifndef FILE_PROVIDER_COMPRESSION_MAXIMUM
#define FILE_PROVIDER_COMPRESSION_MAXIMUM 0x00000004
#endif
#ifndef FILE_PROVIDER_COMPRESSION_XPRESS16K
#define FILE_PROVIDER_COMPRESSION_XPRESS16K 0x00000003
#endif
#ifndef FILE_PROVIDER_COMPRESSION_XPRESS4K
#define FILE_PROVIDER_COMPRESSION_XPRESS4K 0x00000000
#endif
#ifndef FILE_PROVIDER_COMPRESSION_XPRESS8K
#define FILE_PROVIDER_COMPRESSION_XPRESS8K 0x00000002
#endif
#ifndef FILE_PROVIDER_CURRENT_VERSION
#define FILE_PROVIDER_CURRENT_VERSION 0x00000001
#endif
#ifndef FILE_PROVIDER_FLAG_COMPRESS_ON_WRITE
#define FILE_PROVIDER_FLAG_COMPRESS_ON_WRITE 0x00000001
#endif
#ifndef FILE_PROVIDER_SINGLE_FILE
#define FILE_PROVIDER_SINGLE_FILE 0x00000001
#endif
#ifndef FILE_REGION_USAGE_HUGE_PAGE_ALIGNMENT
#define FILE_REGION_USAGE_HUGE_PAGE_ALIGNMENT 0x00000010
#endif
#ifndef FILE_REGION_USAGE_LARGE_PAGE_ALIGNMENT
#define FILE_REGION_USAGE_LARGE_PAGE_ALIGNMENT 0x00000008
#endif
#ifndef FILE_REGION_USAGE_OTHER_PAGE_ALIGNMENT
#define FILE_REGION_USAGE_OTHER_PAGE_ALIGNMENT 0x00000004
#endif
#ifndef FILE_REGION_USAGE_VALID_CACHED_DATA
#define FILE_REGION_USAGE_VALID_CACHED_DATA 0x00000001
#endif
#ifndef FILE_REGION_USAGE_VALID_NONCACHED_DATA
#define FILE_REGION_USAGE_VALID_NONCACHED_DATA 0x00000002
#endif
#ifndef FILE_RETURNS_CLEANUP_RESULT_INFO
#define FILE_RETURNS_CLEANUP_RESULT_INFO 0x00000200
#endif
#ifndef FILE_STORAGE_TIER_DESCRIPTION_LENGTH
#define FILE_STORAGE_TIER_DESCRIPTION_LENGTH 512
#endif
#ifndef FILE_STORAGE_TIER_FLAG_NO_SEEK_PENALTY
#define FILE_STORAGE_TIER_FLAG_NO_SEEK_PENALTY 0x00020000
#endif
#ifndef FILE_STORAGE_TIER_FLAG_PARITY
#define FILE_STORAGE_TIER_FLAG_PARITY 0x00800000
#endif
#ifndef FILE_STORAGE_TIER_FLAG_READ_CACHE
#define FILE_STORAGE_TIER_FLAG_READ_CACHE 0x00400000
#endif
#ifndef FILE_STORAGE_TIER_FLAG_SMR
#define FILE_STORAGE_TIER_FLAG_SMR 0x01000000
#endif
#ifndef FILE_STORAGE_TIER_FLAG_WRITE_BACK_CACHE
#define FILE_STORAGE_TIER_FLAG_WRITE_BACK_CACHE 0x00200000
#endif
#ifndef FILE_STORAGE_TIER_NAME_LENGTH
#define FILE_STORAGE_TIER_NAME_LENGTH 256
#endif
#ifndef FILE_SUPPORTS_BYPASS_IO
#define FILE_SUPPORTS_BYPASS_IO 0x00000800
#endif
#ifndef FILE_SUPPORTS_GHOSTING
#define FILE_SUPPORTS_GHOSTING 0x40000000
#endif
#ifndef FILE_SUPPORTS_POSIX_UNLINK_RENAME
#define FILE_SUPPORTS_POSIX_UNLINK_RENAME 0x00000400
#endif
#ifndef FILE_SUPPORTS_STREAM_SNAPSHOTS
#define FILE_SUPPORTS_STREAM_SNAPSHOTS 0x00001000
#endif
#ifndef FILE_ZERO_DATA_INFORMATION_FLAG_PRESERVE_CACHED_DATA
#define FILE_ZERO_DATA_INFORMATION_FLAG_PRESERVE_CACHED_DATA 0x00000001
#endif
#ifndef FLAGS_DELAY_REASONS_BITMAP_SCANNED
#define FLAGS_DELAY_REASONS_BITMAP_SCANNED 0x00000002
#endif
#ifndef FLAGS_DELAY_REASONS_LOG_FILE_FULL
#define FLAGS_DELAY_REASONS_LOG_FILE_FULL 0x00000001
#endif
#ifndef FLAGS_END_OF_FILE_INFO_EX_EXTEND_PAGING
#define FLAGS_END_OF_FILE_INFO_EX_EXTEND_PAGING 0x00000001
#endif
#ifndef FLAGS_END_OF_FILE_INFO_EX_NO_EXTRA_PAGING_EXTEND
#define FLAGS_END_OF_FILE_INFO_EX_NO_EXTRA_PAGING_EXTEND 0x00000002
#endif
#ifndef FLAGS_END_OF_FILE_INFO_EX_TIME_CONSTRAINED
#define FLAGS_END_OF_FILE_INFO_EX_TIME_CONSTRAINED 0x00000004
#endif
#ifndef FLAG_USN_TRACK_MODIFIED_RANGES_ENABLE
#define FLAG_USN_TRACK_MODIFIED_RANGES_ENABLE 0x00000001
#endif
#ifndef FLUSH_FLAGS_FILE_DATA_ONLY
#define FLUSH_FLAGS_FILE_DATA_ONLY 0x00000001
#endif
#ifndef FLUSH_FLAGS_FILE_DATA_SYNC_ONLY
#define FLUSH_FLAGS_FILE_DATA_SYNC_ONLY 0x00000004
#endif
#ifndef FLUSH_FLAGS_FLUSH_AND_PURGE
#define FLUSH_FLAGS_FLUSH_AND_PURGE 0x00000008
#endif
#ifndef FLUSH_FLAGS_NO_SYNC
#define FLUSH_FLAGS_NO_SYNC 0x00000002
#endif
#ifndef FSCTL_INTEGRITY_FLAG_CHECKSUM_ENFORCEMENT_OFF
#define FSCTL_INTEGRITY_FLAG_CHECKSUM_ENFORCEMENT_OFF 1
#endif
#ifndef FSRTL_ADD_TC_CASE_SENSITIVE
#define FSRTL_ADD_TC_CASE_SENSITIVE 0x00000001
#endif
#ifndef FSRTL_ADD_TC_KEY_BY_SHORT_NAME
#define FSRTL_ADD_TC_KEY_BY_SHORT_NAME 0x00000002
#endif
#ifndef FSRTL_DRIVER_BACKING_FLAG_USE_PAGE_FILE
#define FSRTL_DRIVER_BACKING_FLAG_USE_PAGE_FILE 0x00000001
#endif
#ifndef FSRTL_FIND_TC_CASE_SENSITIVE
#define FSRTL_FIND_TC_CASE_SENSITIVE 0x00000001
#endif
#ifndef FSRTL_FLAG2_BYPASSIO_STREAM_PAUSED
#define FSRTL_FLAG2_BYPASSIO_STREAM_PAUSED 0x20
#endif
#ifndef FSRTL_FLAG2_WRITABLE_USER_MAPPED_FILE
#define FSRTL_FLAG2_WRITABLE_USER_MAPPED_FILE 0x10
#endif
#ifndef FSRTL_UNC_HARDENING_CAPABILITIES_INTEGRITY
#define FSRTL_UNC_HARDENING_CAPABILITIES_INTEGRITY 0x00000002
#endif
#ifndef FSRTL_UNC_HARDENING_CAPABILITIES_MUTUAL_AUTH
#define FSRTL_UNC_HARDENING_CAPABILITIES_MUTUAL_AUTH 0x00000001
#endif
#ifndef FSRTL_UNC_HARDENING_CAPABILITIES_PRIVACY
#define FSRTL_UNC_HARDENING_CAPABILITIES_PRIVACY 0x00000004
#endif
#ifndef FSRTL_UNC_PROVIDER_FLAGS_CONTAINER_AWARE
#define FSRTL_UNC_PROVIDER_FLAGS_CONTAINER_AWARE 0x00000008
#endif
#ifndef FSRTL_UNC_REGISTRATION_VERSION_0200
#define FSRTL_UNC_REGISTRATION_VERSION_0200 0x0200
#endif
#ifndef FSRTL_UNC_REGISTRATION_VERSION_0201
#define FSRTL_UNC_REGISTRATION_VERSION_0201 0x0201
#endif
#ifndef GCR_VSM_PROTECTED_PASSWORD
#define GCR_VSM_PROTECTED_PASSWORD 0x4000
#endif
#ifndef GET_VOLUME_BITMAP_FLAG_MASK_METADATA
#define GET_VOLUME_BITMAP_FLAG_MASK_METADATA 0x00000001
#endif
#ifndef IO_CREATE_STREAM_FILE_LITE
#define IO_CREATE_STREAM_FILE_LITE 0x0002
#endif
#ifndef IO_CREATE_STREAM_FILE_RAISE_ON_ERROR
#define IO_CREATE_STREAM_FILE_RAISE_ON_ERROR 0x0001
#endif
#ifndef IO_IGNORE_READONLY_ATTRIBUTE
#define IO_IGNORE_READONLY_ATTRIBUTE 0x0040
#endif
#ifndef IO_QOS_MAX_RESERVATION
#define IO_QOS_MAX_RESERVATION 1000000000ULL
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_0
#define IO_REPARSE_TAG_ACRONIS_HSM_0 0x00000060L
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_1
#define IO_REPARSE_TAG_ACRONIS_HSM_1 0x00000061L
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_2
#define IO_REPARSE_TAG_ACRONIS_HSM_2 0x00000062L
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_3
#define IO_REPARSE_TAG_ACRONIS_HSM_3 0x00000063L
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_4
#define IO_REPARSE_TAG_ACRONIS_HSM_4 0x00000064L
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_5
#define IO_REPARSE_TAG_ACRONIS_HSM_5 0x00000065L
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_6
#define IO_REPARSE_TAG_ACRONIS_HSM_6 0x00000066L
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_7
#define IO_REPARSE_TAG_ACRONIS_HSM_7 0x00000067L
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_8
#define IO_REPARSE_TAG_ACRONIS_HSM_8 0x00000068L
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_9
#define IO_REPARSE_TAG_ACRONIS_HSM_9 0x00000069L
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_A
#define IO_REPARSE_TAG_ACRONIS_HSM_A 0x0000006AL
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_B
#define IO_REPARSE_TAG_ACRONIS_HSM_B 0x0000006BL
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_C
#define IO_REPARSE_TAG_ACRONIS_HSM_C 0x0000006CL
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_D
#define IO_REPARSE_TAG_ACRONIS_HSM_D 0x0000006DL
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_E
#define IO_REPARSE_TAG_ACRONIS_HSM_E 0x0000006EL
#endif
#ifndef IO_REPARSE_TAG_ACRONIS_HSM_F
#define IO_REPARSE_TAG_ACRONIS_HSM_F 0x0000006FL
#endif
#ifndef IO_REPARSE_TAG_ACTIVISION_HSM
#define IO_REPARSE_TAG_ACTIVISION_HSM 0x00000047L
#endif
#ifndef IO_REPARSE_TAG_ADA_HSM
#define IO_REPARSE_TAG_ADA_HSM 0x00000026L
#endif
#ifndef IO_REPARSE_TAG_ADOBE_HSM
#define IO_REPARSE_TAG_ADOBE_HSM 0x00000045L
#endif
#ifndef IO_REPARSE_TAG_ALERTBOOT
#define IO_REPARSE_TAG_ALERTBOOT 0x2000004CL
#endif
#ifndef IO_REPARSE_TAG_ALTIRIS_HSM
#define IO_REPARSE_TAG_ALTIRIS_HSM 0x00000019L
#endif
#ifndef IO_REPARSE_TAG_AMZN_APPSTREAM
#define IO_REPARSE_TAG_AMZN_APPSTREAM 0x00000059L
#endif
#ifndef IO_REPARSE_TAG_APPEXECLINK
#define IO_REPARSE_TAG_APPEXECLINK 0x8000001BL
#endif
#ifndef IO_REPARSE_TAG_APPXSTRM
#define IO_REPARSE_TAG_APPXSTRM 0xC0000014L
#endif
#ifndef IO_REPARSE_TAG_ARCO_BACKUP
#define IO_REPARSE_TAG_ARCO_BACKUP 0x0000003BL
#endif
#ifndef IO_REPARSE_TAG_ARKIVIO
#define IO_REPARSE_TAG_ARKIVIO 0x0000000CL
#endif
#ifndef IO_REPARSE_TAG_AURISTOR_FS
#define IO_REPARSE_TAG_AURISTOR_FS 0x00000049L
#endif
#ifndef IO_REPARSE_TAG_AUTN_HSM
#define IO_REPARSE_TAG_AUTN_HSM 0x00000027L
#endif
#ifndef IO_REPARSE_TAG_BRIDGEHEAD_HSM
#define IO_REPARSE_TAG_BRIDGEHEAD_HSM 0x00000016L
#endif
#ifndef IO_REPARSE_TAG_C2CSYSTEMS_HSM
#define IO_REPARSE_TAG_C2CSYSTEMS_HSM 0x00000031L
#endif
#ifndef IO_REPARSE_TAG_CARINGO_HSM
#define IO_REPARSE_TAG_CARINGO_HSM 0x00000034L
#endif
#ifndef IO_REPARSE_TAG_CARROLL_HSM
#define IO_REPARSE_TAG_CARROLL_HSM 0x0000003CL
#endif
#ifndef IO_REPARSE_TAG_CITRIX_PM
#define IO_REPARSE_TAG_CITRIX_PM 0x00000036L
#endif
#ifndef IO_REPARSE_TAG_CLOUD
#define IO_REPARSE_TAG_CLOUD 0x9000001AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_1
#define IO_REPARSE_TAG_CLOUD_1 0x9000101AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_2
#define IO_REPARSE_TAG_CLOUD_2 0x9000201AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_3
#define IO_REPARSE_TAG_CLOUD_3 0x9000301AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_4
#define IO_REPARSE_TAG_CLOUD_4 0x9000401AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_5
#define IO_REPARSE_TAG_CLOUD_5 0x9000501AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_6
#define IO_REPARSE_TAG_CLOUD_6 0x9000601AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_7
#define IO_REPARSE_TAG_CLOUD_7 0x9000701AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_8
#define IO_REPARSE_TAG_CLOUD_8 0x9000801AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_9
#define IO_REPARSE_TAG_CLOUD_9 0x9000901AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_A
#define IO_REPARSE_TAG_CLOUD_A 0x9000A01AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_B
#define IO_REPARSE_TAG_CLOUD_B 0x9000B01AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_C
#define IO_REPARSE_TAG_CLOUD_C 0x9000C01AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_D
#define IO_REPARSE_TAG_CLOUD_D 0x9000D01AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_E
#define IO_REPARSE_TAG_CLOUD_E 0x9000E01AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_F
#define IO_REPARSE_TAG_CLOUD_F 0x9000F01AL
#endif
#ifndef IO_REPARSE_TAG_CLOUD_MASK
#define IO_REPARSE_TAG_CLOUD_MASK 0x0000F000L
#endif
#ifndef IO_REPARSE_TAG_COMMVAULT
#define IO_REPARSE_TAG_COMMVAULT 0x0000000EL
#endif
#ifndef IO_REPARSE_TAG_COMMVAULT_HSM
#define IO_REPARSE_TAG_COMMVAULT_HSM 0x0000001DL
#endif
#ifndef IO_REPARSE_TAG_COMTRADE_HSM
#define IO_REPARSE_TAG_COMTRADE_HSM 0x0000003DL
#endif
#ifndef IO_REPARSE_TAG_CTERA_HSM
#define IO_REPARSE_TAG_CTERA_HSM 0x0000004EL
#endif
#ifndef IO_REPARSE_TAG_DATAFIRST_HSM
#define IO_REPARSE_TAG_DATAFIRST_HSM 0x00000030L
#endif
#ifndef IO_REPARSE_TAG_DATAGLOBAL_HSM
#define IO_REPARSE_TAG_DATAGLOBAL_HSM 0x0000002EL
#endif
#ifndef IO_REPARSE_TAG_DATALESS_CIM
#define IO_REPARSE_TAG_DATALESS_CIM 0xA0000028L
#endif
#ifndef IO_REPARSE_TAG_DATASTOR_SIS
#define IO_REPARSE_TAG_DATASTOR_SIS 0x0000001EL
#endif
#ifndef IO_REPARSE_TAG_DEDUP
#define IO_REPARSE_TAG_DEDUP 0x80000013L
#endif
#ifndef IO_REPARSE_TAG_DFM
#define IO_REPARSE_TAG_DFM 0x80000016L
#endif
#ifndef IO_REPARSE_TAG_DOR_HSM
#define IO_REPARSE_TAG_DOR_HSM 0x00000052L
#endif
#ifndef IO_REPARSE_TAG_DOUBLE_TAKE_HSM
#define IO_REPARSE_TAG_DOUBLE_TAKE_HSM 0x00000022L
#endif
#ifndef IO_REPARSE_TAG_DOUBLE_TAKE_SIS
#define IO_REPARSE_TAG_DOUBLE_TAKE_SIS 0x00000029L
#endif
#ifndef IO_REPARSE_TAG_DROPBOX_HSM
#define IO_REPARSE_TAG_DROPBOX_HSM 0x00000044L
#endif
#ifndef IO_REPARSE_TAG_EASEFILTER_HSM
#define IO_REPARSE_TAG_EASEFILTER_HSM 0x00000057L
#endif
#ifndef IO_REPARSE_TAG_EASEVAULT_HSM
#define IO_REPARSE_TAG_EASEVAULT_HSM 0x0000003EL
#endif
#ifndef IO_REPARSE_TAG_EDSI_HSM
#define IO_REPARSE_TAG_EDSI_HSM 0x0000001FL
#endif
#ifndef IO_REPARSE_TAG_ELTAN_HSM
#define IO_REPARSE_TAG_ELTAN_HSM 0x0000002BL
#endif
#ifndef IO_REPARSE_TAG_EMC_HSM
#define IO_REPARSE_TAG_EMC_HSM 0x00000039L
#endif
#ifndef IO_REPARSE_TAG_ENIGMA_HSM
#define IO_REPARSE_TAG_ENIGMA_HSM 0x00000011L
#endif
#ifndef IO_REPARSE_TAG_FILE_PLACEHOLDER
#define IO_REPARSE_TAG_FILE_PLACEHOLDER 0x80000015L
#endif
#ifndef IO_REPARSE_TAG_GLOBAL360_HSM
#define IO_REPARSE_TAG_GLOBAL360_HSM 0x00000018L
#endif
#ifndef IO_REPARSE_TAG_GLOBAL_REPARSE
#define IO_REPARSE_TAG_GLOBAL_REPARSE 0xA0000019L
#endif
#ifndef IO_REPARSE_TAG_GOOGLE_HSM
#define IO_REPARSE_TAG_GOOGLE_HSM 0x00000041L
#endif
#ifndef IO_REPARSE_TAG_GRAU_DATASTORAGE_HSM
#define IO_REPARSE_TAG_GRAU_DATASTORAGE_HSM 0x0000001CL
#endif
#ifndef IO_REPARSE_TAG_HDS_HCP_HSM
#define IO_REPARSE_TAG_HDS_HCP_HSM 0x00000048L
#endif
#ifndef IO_REPARSE_TAG_HDS_HSM
#define IO_REPARSE_TAG_HDS_HSM 0x0000003FL
#endif
#ifndef IO_REPARSE_TAG_HERMES_HSM
#define IO_REPARSE_TAG_HERMES_HSM 0x0000001AL
#endif
#ifndef IO_REPARSE_TAG_HP_BACKUP
#define IO_REPARSE_TAG_HP_BACKUP 0x00000043L
#endif
#ifndef IO_REPARSE_TAG_HP_DATA_PROTECT
#define IO_REPARSE_TAG_HP_DATA_PROTECT 0x00000046L
#endif
#ifndef IO_REPARSE_TAG_HP_HSM
#define IO_REPARSE_TAG_HP_HSM 0x00000020L
#endif
#ifndef IO_REPARSE_TAG_HSAG_HSM
#define IO_REPARSE_TAG_HSAG_HSM 0x00000025L
#endif
#ifndef IO_REPARSE_TAG_HUBSTOR_HSM
#define IO_REPARSE_TAG_HUBSTOR_HSM 0x00000055L
#endif
#ifndef IO_REPARSE_TAG_IFSTEST_CONGRUENT
#define IO_REPARSE_TAG_IFSTEST_CONGRUENT 0x00000009L
#endif
#ifndef IO_REPARSE_TAG_IMANAGE_HSM
#define IO_REPARSE_TAG_IMANAGE_HSM 0x20000056L
#endif
#ifndef IO_REPARSE_TAG_INTERCOPE_HSM
#define IO_REPARSE_TAG_INTERCOPE_HSM 0x00000013L
#endif
#ifndef IO_REPARSE_TAG_ITSTATION
#define IO_REPARSE_TAG_ITSTATION 0x0000004AL
#endif
#ifndef IO_REPARSE_TAG_KOM_NETWORKS_HSM
#define IO_REPARSE_TAG_KOM_NETWORKS_HSM 0x00000014L
#endif
#ifndef IO_REPARSE_TAG_MAGINATICS_RDR
#define IO_REPARSE_TAG_MAGINATICS_RDR 0x00000040L
#endif
#ifndef IO_REPARSE_TAG_MAXISCALE_HSM
#define IO_REPARSE_TAG_MAXISCALE_HSM 0x20000035L
#endif
#ifndef IO_REPARSE_TAG_MEMORY_TECH_HSM
#define IO_REPARSE_TAG_MEMORY_TECH_HSM 0x00000015L
#endif
#ifndef IO_REPARSE_TAG_MIMOSA_HSM
#define IO_REPARSE_TAG_MIMOSA_HSM 0x00000024L
#endif
#ifndef IO_REPARSE_TAG_MOONWALK_HSM
#define IO_REPARSE_TAG_MOONWALK_HSM 0x0000000AL
#endif
#ifndef IO_REPARSE_TAG_MTALOS
#define IO_REPARSE_TAG_MTALOS 0x0000004DL
#endif
#ifndef IO_REPARSE_TAG_NEUSHIELD
#define IO_REPARSE_TAG_NEUSHIELD 0x00000051L
#endif
#ifndef IO_REPARSE_TAG_NEXSAN_HSM
#define IO_REPARSE_TAG_NEXSAN_HSM 0x00000028L
#endif
#ifndef IO_REPARSE_TAG_NFS
#define IO_REPARSE_TAG_NFS 0x80000014L
#endif
#ifndef IO_REPARSE_TAG_NIPPON_HSM
#define IO_REPARSE_TAG_NIPPON_HSM 0x0000004FL
#endif
#ifndef IO_REPARSE_TAG_NVIDIA_UNIONFS
#define IO_REPARSE_TAG_NVIDIA_UNIONFS 0x20000054L
#endif
#ifndef IO_REPARSE_TAG_ONEDRIVE
#define IO_REPARSE_TAG_ONEDRIVE 0x80000021L
#endif
#ifndef IO_REPARSE_TAG_OPENAFS_DFS
#define IO_REPARSE_TAG_OPENAFS_DFS 0x00000037L
#endif
#ifndef IO_REPARSE_TAG_OSR_SAMPLE
#define IO_REPARSE_TAG_OSR_SAMPLE 0x20000017L
#endif
#ifndef IO_REPARSE_TAG_OVERTONE
#define IO_REPARSE_TAG_OVERTONE 0x0000000FL
#endif
#ifndef IO_REPARSE_TAG_PEER_GFS
#define IO_REPARSE_TAG_PEER_GFS 0x00000058L
#endif
#ifndef IO_REPARSE_TAG_POINTSOFT_HSM
#define IO_REPARSE_TAG_POINTSOFT_HSM 0x0000001BL
#endif
#ifndef IO_REPARSE_TAG_PROJFS
#define IO_REPARSE_TAG_PROJFS 0x9000001CL
#endif
#ifndef IO_REPARSE_TAG_PROJFS_TOMBSTONE
#define IO_REPARSE_TAG_PROJFS_TOMBSTONE 0xA0000022L
#endif
#ifndef IO_REPARSE_TAG_QI_TECH_HSM
#define IO_REPARSE_TAG_QI_TECH_HSM 0x2000002FL
#endif
#ifndef IO_REPARSE_TAG_QUADDRA_HSM
#define IO_REPARSE_TAG_QUADDRA_HSM 0x00000042L
#endif
#ifndef IO_REPARSE_TAG_QUEST_HSM
#define IO_REPARSE_TAG_QUEST_HSM 0x0000002DL
#endif
#ifndef IO_REPARSE_TAG_REDSTOR_HSM
#define IO_REPARSE_TAG_REDSTOR_HSM 0x00000050L
#endif
#ifndef IO_REPARSE_TAG_RESERVED_INVALID
#define IO_REPARSE_TAG_RESERVED_INVALID 0xC0008000L
#endif
#ifndef IO_REPARSE_TAG_RIVERBED_HSM
#define IO_REPARSE_TAG_RIVERBED_HSM 0x00000033L
#endif
#ifndef IO_REPARSE_TAG_SER_HSM
#define IO_REPARSE_TAG_SER_HSM 0x00000021L
#endif
#ifndef IO_REPARSE_TAG_SHX_BACKUP
#define IO_REPARSE_TAG_SHX_BACKUP 0x00000053L
#endif
#ifndef IO_REPARSE_TAG_SOLUTIONSOFT
#define IO_REPARSE_TAG_SOLUTIONSOFT 0x2000000DL
#endif
#ifndef IO_REPARSE_TAG_SONY_HSM
#define IO_REPARSE_TAG_SONY_HSM 0x0000002AL
#endif
#ifndef IO_REPARSE_TAG_SPHARSOFT
#define IO_REPARSE_TAG_SPHARSOFT 0x0000004BL
#endif
#ifndef IO_REPARSE_TAG_STORAGE_SYNC
#define IO_REPARSE_TAG_STORAGE_SYNC 0x8000001EL
#endif
#ifndef IO_REPARSE_TAG_STORAGE_SYNC_FOLDER
#define IO_REPARSE_TAG_STORAGE_SYNC_FOLDER 0x90000027L
#endif
#ifndef IO_REPARSE_TAG_SYMANTEC_HSM
#define IO_REPARSE_TAG_SYMANTEC_HSM 0x00000012L
#endif
#ifndef IO_REPARSE_TAG_SYMANTEC_HSM2
#define IO_REPARSE_TAG_SYMANTEC_HSM2 0x00000010L
#endif
#ifndef IO_REPARSE_TAG_TSINGHUA_UNIVERSITY_RESEARCH
#define IO_REPARSE_TAG_TSINGHUA_UNIVERSITY_RESEARCH 0x0000000BL
#endif
#ifndef IO_REPARSE_TAG_UNHANDLED
#define IO_REPARSE_TAG_UNHANDLED 0x80000020L
#endif
#ifndef IO_REPARSE_TAG_UTIXO_HSM
#define IO_REPARSE_TAG_UTIXO_HSM 0x0000002CL
#endif
#ifndef IO_REPARSE_TAG_VMWARE_PM
#define IO_REPARSE_TAG_VMWARE_PM 0x0000003AL
#endif
#ifndef IO_REPARSE_TAG_WATERFORD
#define IO_REPARSE_TAG_WATERFORD 0x00000032L
#endif
#ifndef IO_REPARSE_TAG_WCI_1
#define IO_REPARSE_TAG_WCI_1 0x90001018L
#endif
#ifndef IO_REPARSE_TAG_WCI_LINK
#define IO_REPARSE_TAG_WCI_LINK 0xA0000027L
#endif
#ifndef IO_REPARSE_TAG_WCI_LINK_1
#define IO_REPARSE_TAG_WCI_LINK_1 0xA0001027L
#endif
#ifndef IO_REPARSE_TAG_WCI_TOMBSTONE
#define IO_REPARSE_TAG_WCI_TOMBSTONE 0xA000001FL
#endif
#ifndef IO_REPARSE_TAG_WISDATA_HSM
#define IO_REPARSE_TAG_WISDATA_HSM 0x00000023L
#endif
#ifndef IO_REPARSE_TAG_WOF
#define IO_REPARSE_TAG_WOF 0x80000017L
#endif
#ifndef IO_REPARSE_TAG_ZLTI_HSM
#define IO_REPARSE_TAG_ZLTI_HSM 0x00000038L
#endif
#ifndef ISC_REQ_CONFIDENTIALITY_ONLY
#define ISC_REQ_CONFIDENTIALITY_ONLY 0x40000000
#endif
#ifndef ISC_REQ_DEFERRED_CRED_VALIDATION
#define ISC_REQ_DEFERRED_CRED_VALIDATION 0x0000000200000000
#endif
#ifndef ISC_REQ_EXPLICIT_SESSION
#define ISC_REQ_EXPLICIT_SESSION 0x0000001000000000
#endif
#ifndef ISC_REQ_MESSAGES
#define ISC_REQ_MESSAGES 0x0000000100000000
#endif
#ifndef ISC_REQ_NO_POST_HANDSHAKE_AUTH
#define ISC_REQ_NO_POST_HANDSHAKE_AUTH 0x0000000400000000
#endif
#ifndef ISC_REQ_REUSE_SESSION_TICKETS
#define ISC_REQ_REUSE_SESSION_TICKETS 0x0000000800000000
#endif
#ifndef ISC_REQ_UNVERIFIED_TARGET_NAME
#define ISC_REQ_UNVERIFIED_TARGET_NAME 0x20000000
#endif
#ifndef ISC_RET_CONFIDENTIALITY_ONLY
#define ISC_RET_CONFIDENTIALITY_ONLY 0x40000000
#endif
#ifndef ISC_RET_DEFERRED_CRED_VALIDATION
#define ISC_RET_DEFERRED_CRED_VALIDATION 0x0000000200000000
#endif
#ifndef ISC_RET_EXPLICIT_SESSION
#define ISC_RET_EXPLICIT_SESSION 0x0000001000000000
#endif
#ifndef ISC_RET_MESSAGES
#define ISC_RET_MESSAGES 0x0000000100000000
#endif
#ifndef ISC_RET_NO_POST_HANDSHAKE_AUTH
#define ISC_RET_NO_POST_HANDSHAKE_AUTH 0x0000000400000000
#endif
#ifndef ISC_RET_REUSE_SESSION_TICKETS
#define ISC_RET_REUSE_SESSION_TICKETS 0x0000000800000000
#endif
#ifndef KDC_NETWORK_DISCOVERY_FLAGS_DS13_REQUIRED
#define KDC_NETWORK_DISCOVERY_FLAGS_DS13_REQUIRED 0x80000000
#endif
#ifndef KDC_NETWORK_SETTINGS_FLAGS_CONFIGURE_DISCOVERY
#define KDC_NETWORK_SETTINGS_FLAGS_CONFIGURE_DISCOVERY 0x40000000
#endif
#ifndef KDC_NETWORK_SETTINGS_FLAGS_CONFIGURE_PROXY
#define KDC_NETWORK_SETTINGS_FLAGS_CONFIGURE_PROXY 0x80000000
#endif
#ifndef KDC_NETWORK_SETTINGS_FLAGS_FORCEPROXY
#define KDC_NETWORK_SETTINGS_FLAGS_FORCEPROXY 0x1
#endif
#ifndef KDC_NETWORK_SETTINGS_V2
#define KDC_NETWORK_SETTINGS_V2 2
#endif
#ifndef KDC_PROXY_SETTINGS_FLAGS_FORCEPROXY
#define KDC_PROXY_SETTINGS_FLAGS_FORCEPROXY 0x1
#endif
#ifndef LOGON_MANAGED_SERVICE
#define LOGON_MANAGED_SERVICE 0x80000
#endif
#ifndef LOGON_NO_ELEVATION
#define LOGON_NO_ELEVATION 0x40000
#endif
#ifndef MAP_DISABLE_PAGEFAULT_CLUSTERING
#define MAP_DISABLE_PAGEFAULT_CLUSTERING 0x00000100
#endif
#ifndef MARK_HANDLE_CLOUD_SYNC
#define MARK_HANDLE_CLOUD_SYNC 0x00000800
#endif
#ifndef MARK_HANDLE_DISABLE_FILE_METADATA_OPTIMIZATION
#define MARK_HANDLE_DISABLE_FILE_METADATA_OPTIMIZATION 0x00001000
#endif
#ifndef MARK_HANDLE_ENABLE_CPU_CACHE
#define MARK_HANDLE_ENABLE_CPU_CACHE 0x10000000
#endif
#ifndef MARK_HANDLE_ENABLE_USN_SOURCE_ON_PAGING_IO
#define MARK_HANDLE_ENABLE_USN_SOURCE_ON_PAGING_IO 0x00002000
#endif
#ifndef MARK_HANDLE_FILTER_METADATA
#define MARK_HANDLE_FILTER_METADATA 0x00000200
#endif
#ifndef MARK_HANDLE_NOT_READ_COPY
#define MARK_HANDLE_NOT_READ_COPY 0x00000100
#endif
#ifndef MARK_HANDLE_READ_COPY
#define MARK_HANDLE_READ_COPY 0x00000080
#endif
#ifndef MARK_HANDLE_RETURN_PURGE_FAILURE
#define MARK_HANDLE_RETURN_PURGE_FAILURE 0x00000400
#endif
#ifndef MARK_HANDLE_SKIP_COHERENCY_SYNC_DISALLOW_WRITES
#define MARK_HANDLE_SKIP_COHERENCY_SYNC_DISALLOW_WRITES 0x00004000
#endif
#ifndef MARK_HANDLE_SUPPRESS_VOLUME_OPEN_FLUSH
#define MARK_HANDLE_SUPPRESS_VOLUME_OPEN_FLUSH 0x00008000
#endif
#ifndef MM_IS_FILE_SECTION_ACTIVE_DATA
#define MM_IS_FILE_SECTION_ACTIVE_DATA 0x2
#endif
#ifndef MM_IS_FILE_SECTION_ACTIVE_IMAGE
#define MM_IS_FILE_SECTION_ACTIVE_IMAGE 0x1
#endif
#ifndef MM_IS_FILE_SECTION_ACTIVE_USER
#define MM_IS_FILE_SECTION_ACTIVE_USER 0x4
#endif
#ifndef MSV1_0_AV_FLAG_UNVERIFIED_TARGET
#define MSV1_0_AV_FLAG_UNVERIFIED_TARGET 0x00000004
#endif
#ifndef MSV1_0_CREDENTIAL_KEY_LENGTH
#define MSV1_0_CREDENTIAL_KEY_LENGTH 20
#endif
#ifndef MSV1_0_CRED_CREDKEY_PRESENT
#define MSV1_0_CRED_CREDKEY_PRESENT 0x0008
#endif
#ifndef MSV1_0_CRED_REMOVED
#define MSV1_0_CRED_REMOVED 0x0004
#endif
#ifndef MSV1_0_CRED_SHA_PRESENT
#define MSV1_0_CRED_SHA_PRESENT 0x0010
#endif
#ifndef MSV1_0_CRED_VERSION_ARSO
#define MSV1_0_CRED_VERSION_ARSO 0xffff0003
#endif
#ifndef MSV1_0_CRED_VERSION_INVALID
#define MSV1_0_CRED_VERSION_INVALID 0xffffffff
#endif
#ifndef MSV1_0_CRED_VERSION_IUM
#define MSV1_0_CRED_VERSION_IUM 0xffff0001
#endif
#ifndef MSV1_0_CRED_VERSION_REMOTE
#define MSV1_0_CRED_VERSION_REMOTE 0xffff0002
#endif
#ifndef MSV1_0_CRED_VERSION_RESERVED_1
#define MSV1_0_CRED_VERSION_RESERVED_1 0xfffffffe
#endif
#ifndef MSV1_0_CRED_VERSION_V2
#define MSV1_0_CRED_VERSION_V2 2
#endif
#ifndef MSV1_0_CRED_VERSION_V3
#define MSV1_0_CRED_VERSION_V3 4
#endif
#ifndef MSV1_0_INTERNET_DOMAIN
#define MSV1_0_INTERNET_DOMAIN 0x00080000
#endif
#ifndef MSV1_0_SHA_PASSWORD_LENGTH
#define MSV1_0_SHA_PASSWORD_LENGTH 20
#endif
#ifndef NETWORK_APP_INSTANCE_CSV_FLAGS_VALID_ONLY_IF_CSV_COORDINATOR
#define NETWORK_APP_INSTANCE_CSV_FLAGS_VALID_ONLY_IF_CSV_COORDINATOR 0x00000001
#endif
#ifndef NETWORK_OPEN_ECP_IN_FLAG_DISABLE_OPLOCKS
#define NETWORK_OPEN_ECP_IN_FLAG_DISABLE_OPLOCKS 0x4
#endif
#ifndef NETWORK_OPEN_ECP_IN_FLAG_FORCE_MAX_EOF_HACK
#define NETWORK_OPEN_ECP_IN_FLAG_FORCE_MAX_EOF_HACK 0x40000000
#endif
#ifndef NETWORK_OPEN_ECP_IN_FLAG_REQ_MUTUAL_AUTH
#define NETWORK_OPEN_ECP_IN_FLAG_REQ_MUTUAL_AUTH 0x8
#endif
#ifndef NETWORK_OPEN_ECP_OUT_FLAG_RET_MUTUAL_AUTH
#define NETWORK_OPEN_ECP_OUT_FLAG_RET_MUTUAL_AUTH 0x8
#endif
#ifndef NO_LONG_NAMES
#define NO_LONG_NAMES 2
#endif
#ifndef OFFLOAD_READ_FLAG_ALL_ZERO_BEYOND_CURRENT_RANGE
#define OFFLOAD_READ_FLAG_ALL_ZERO_BEYOND_CURRENT_RANGE 1
#endif
#ifndef OPEN_REPARSE_POINT_OVERRIDE_CREATE_OPTION
#define OPEN_REPARSE_POINT_OVERRIDE_CREATE_OPTION 0x00000040
#endif
#ifndef OPEN_REPARSE_POINT_REPARSE_IF_CHILD_EXISTS
#define OPEN_REPARSE_POINT_REPARSE_IF_CHILD_EXISTS 0x00000002
#endif
#ifndef OPEN_REPARSE_POINT_REPARSE_IF_CHILD_NOT_EXISTS
#define OPEN_REPARSE_POINT_REPARSE_IF_CHILD_NOT_EXISTS 0x00000004
#endif
#ifndef OPEN_REPARSE_POINT_REPARSE_IF_DIRECTORY_FINAL_COMPONENT
#define OPEN_REPARSE_POINT_REPARSE_IF_DIRECTORY_FINAL_COMPONENT 0x00000008
#endif
#ifndef OPEN_REPARSE_POINT_REPARSE_IF_NON_DIRECTORY_FINAL_COMPONENT
#define OPEN_REPARSE_POINT_REPARSE_IF_NON_DIRECTORY_FINAL_COMPONENT 0x00000020
#endif
#ifndef OPEN_REPARSE_POINT_REPARSE_IF_NON_DIRECTORY_NON_FINAL_COMPONENT
#define OPEN_REPARSE_POINT_REPARSE_IF_NON_DIRECTORY_NON_FINAL_COMPONENT 0x00000010
#endif
#ifndef OPEN_REPARSE_POINT_RETURN_REPARSE_DATA_BUFFER
#define OPEN_REPARSE_POINT_RETURN_REPARSE_DATA_BUFFER 0x00000080
#endif
#ifndef OPEN_REPARSE_POINT_TAG_ENCOUNTERED
#define OPEN_REPARSE_POINT_TAG_ENCOUNTERED 0x00000001
#endif
#ifndef OPLOCK_FLAG_BREAKING_FOR_SHARING_VIOLATION
#define OPLOCK_FLAG_BREAKING_FOR_SHARING_VIOLATION 0x00000080
#endif
#ifndef OPLOCK_UPPER_FLAG_CHECK_NO_BREAK
#define OPLOCK_UPPER_FLAG_CHECK_NO_BREAK 0x00010000
#endif
#ifndef OPLOCK_UPPER_FLAG_NOTIFY_REFRESH_READ
#define OPLOCK_UPPER_FLAG_NOTIFY_REFRESH_READ 0x00020000
#endif
#ifndef PERSISTENT_VOLUME_STATE_BACKED_BY_WIM
#define PERSISTENT_VOLUME_STATE_BACKED_BY_WIM 0x00000040
#endif
#ifndef PERSISTENT_VOLUME_STATE_CHKDSK_RAN_ONCE
#define PERSISTENT_VOLUME_STATE_CHKDSK_RAN_ONCE 0x00000400
#endif
#ifndef PERSISTENT_VOLUME_STATE_CONTAINS_BACKING_WIM
#define PERSISTENT_VOLUME_STATE_CONTAINS_BACKING_WIM 0x00000020
#endif
#ifndef PERSISTENT_VOLUME_STATE_DAX_FORMATTED
#define PERSISTENT_VOLUME_STATE_DAX_FORMATTED 0x00001000
#endif
#ifndef PERSISTENT_VOLUME_STATE_DEV_VOLUME
#define PERSISTENT_VOLUME_STATE_DEV_VOLUME 0x00002000
#endif
#ifndef PERSISTENT_VOLUME_STATE_GLOBAL_METADATA_NO_SEEK_PENALTY
#define PERSISTENT_VOLUME_STATE_GLOBAL_METADATA_NO_SEEK_PENALTY 0x00000004
#endif
#ifndef PERSISTENT_VOLUME_STATE_LOCAL_METADATA_NO_SEEK_PENALTY
#define PERSISTENT_VOLUME_STATE_LOCAL_METADATA_NO_SEEK_PENALTY 0x00000008
#endif
#ifndef PERSISTENT_VOLUME_STATE_MODIFIED_BY_CHKDSK
#define PERSISTENT_VOLUME_STATE_MODIFIED_BY_CHKDSK 0x00000800
#endif
#ifndef PERSISTENT_VOLUME_STATE_NO_HEAT_GATHERING
#define PERSISTENT_VOLUME_STATE_NO_HEAT_GATHERING 0x00000010
#endif
#ifndef PERSISTENT_VOLUME_STATE_NO_WRITE_AUTO_TIERING
#define PERSISTENT_VOLUME_STATE_NO_WRITE_AUTO_TIERING 0x00000080
#endif
#ifndef PERSISTENT_VOLUME_STATE_REALLOCATE_ALL_DATA_WRITES
#define PERSISTENT_VOLUME_STATE_REALLOCATE_ALL_DATA_WRITES 0x00000200
#endif
#ifndef PERSISTENT_VOLUME_STATE_TRUSTED_VOLUME
#define PERSISTENT_VOLUME_STATE_TRUSTED_VOLUME 0x00004000
#endif
#ifndef PERSISTENT_VOLUME_STATE_TXF_DISABLED
#define PERSISTENT_VOLUME_STATE_TXF_DISABLED 0x00000100
#endif
#ifndef PERSISTENT_VOLUME_STATE_VOLUME_SCRUB_DISABLED
#define PERSISTENT_VOLUME_STATE_VOLUME_SCRUB_DISABLED 0x00000002
#endif
#ifndef PIN_VERIFY_REQUIRED
#define PIN_VERIFY_REQUIRED 0x00000080
#endif
#ifndef PROJFS_PROTOCOL_VERSION
#define PROJFS_PROTOCOL_VERSION 3
#endif
#ifndef PSMP_MAXIMUM_SYSAPP_CLAIM_VALUES
#define PSMP_MAXIMUM_SYSAPP_CLAIM_VALUES 4
#endif
#ifndef PSMP_MINIMUM_SYSAPP_CLAIM_VALUES
#define PSMP_MINIMUM_SYSAPP_CLAIM_VALUES 2
#endif
#ifndef PURGE_WITH_ACTIVE_VIEWS
#define PURGE_WITH_ACTIVE_VIEWS 8
#endif
#ifndef QUERY_DIRECT_ACCESS_DATA_EXTENTS
#define QUERY_DIRECT_ACCESS_DATA_EXTENTS 0x00000002
#endif
#ifndef QUERY_DIRECT_ACCESS_IMAGE_EXTENTS
#define QUERY_DIRECT_ACCESS_IMAGE_EXTENTS 0x00000001
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_EXTENTS
#define QUERY_FILE_LAYOUT_INCLUDE_EXTENTS 0x00000008
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_EXTRA_INFO
#define QUERY_FILE_LAYOUT_INCLUDE_EXTRA_INFO 0x00000010
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_FILES_WITH_DSC_ATTRIBUTE
#define QUERY_FILE_LAYOUT_INCLUDE_FILES_WITH_DSC_ATTRIBUTE 0x00001000
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_FULL_PATH_IN_NAMES
#define QUERY_FILE_LAYOUT_INCLUDE_FULL_PATH_IN_NAMES 0x00000040
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_NAMES
#define QUERY_FILE_LAYOUT_INCLUDE_NAMES 0x00000002
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_ONLY_FILES_WITH_SPECIFIC_ATTRIBUTES
#define QUERY_FILE_LAYOUT_INCLUDE_ONLY_FILES_WITH_SPECIFIC_ATTRIBUTES 0x00000800
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_STREAMS
#define QUERY_FILE_LAYOUT_INCLUDE_STREAMS 0x00000004
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_STREAMS_WITH_NO_CLUSTERS_ALLOCATED
#define QUERY_FILE_LAYOUT_INCLUDE_STREAMS_WITH_NO_CLUSTERS_ALLOCATED 0x00000020
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION
#define QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION 0x00000080
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_DATA_ATTRIBUTE
#define QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_DATA_ATTRIBUTE 0x00002000
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_DSC_ATTRIBUTE
#define QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_DSC_ATTRIBUTE 0x00000100
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_EA_ATTRIBUTE
#define QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_EA_ATTRIBUTE 0x00008000
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_EFS_ATTRIBUTE
#define QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_EFS_ATTRIBUTE 0x00000400
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_REPARSE_ATTRIBUTE
#define QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_REPARSE_ATTRIBUTE 0x00004000
#endif
#ifndef QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_TXF_ATTRIBUTE
#define QUERY_FILE_LAYOUT_INCLUDE_STREAM_INFORMATION_FOR_TXF_ATTRIBUTE 0x00000200
#endif
#ifndef QUERY_FILE_LAYOUT_REPARSE_DATA_INVALID
#define QUERY_FILE_LAYOUT_REPARSE_DATA_INVALID 0x0001
#endif
#ifndef QUERY_FILE_LAYOUT_REPARSE_TAG_INVALID
#define QUERY_FILE_LAYOUT_REPARSE_TAG_INVALID 0x0002
#endif
#ifndef QUERY_FILE_LAYOUT_RESTART
#define QUERY_FILE_LAYOUT_RESTART 0x00000001
#endif
#ifndef QUERY_FILE_LAYOUT_SINGLE_INSTANCED
#define QUERY_FILE_LAYOUT_SINGLE_INSTANCED 0x00000001
#endif
#ifndef QUERY_STORAGE_CLASSES_FLAGS_MEASURE_READ
#define QUERY_STORAGE_CLASSES_FLAGS_MEASURE_READ 0x40000000
#endif
#ifndef QUERY_STORAGE_CLASSES_FLAGS_MEASURE_WRITE
#define QUERY_STORAGE_CLASSES_FLAGS_MEASURE_WRITE 0x80000000
#endif
#ifndef QUERY_STORAGE_CLASSES_FLAGS_NO_DEFRAG_VOLUME
#define QUERY_STORAGE_CLASSES_FLAGS_NO_DEFRAG_VOLUME 0x20000000
#endif
#ifndef QoCFileEaInformation
#define QoCFileEaInformation 0x00000004
#endif
#ifndef QoCFileLxInformation
#define QoCFileLxInformation 0x00000002
#endif
#ifndef QoCFileSecurityInformation
#define QoCFileSecurityInformation 0x00000010
#endif
#ifndef QoCFileStatInformation
#define QoCFileStatInformation 0x00000001
#endif
#ifndef QoCFileUsnInformation
#define QoCFileUsnInformation 0x00000008
#endif
#ifndef REFS_QUERY_ROLLBACK_PROTECTION_INFO_OUTPUT_BUFFER_VERSION
#define REFS_QUERY_ROLLBACK_PROTECTION_INFO_OUTPUT_BUFFER_VERSION 1
#endif
#ifndef REFS_QUERY_VOLUME_COMPRESSION_INFO_OUTPUT_BUFFER_VERSION
#define REFS_QUERY_VOLUME_COMPRESSION_INFO_OUTPUT_BUFFER_VERSION 1
#endif
#ifndef REFS_QUERY_VOLUME_IO_METRICS_INFO_INPUT_BUFFER_VERSION
#define REFS_QUERY_VOLUME_IO_METRICS_INFO_INPUT_BUFFER_VERSION 1
#endif
#ifndef REFS_QUERY_VOLUME_IO_METRICS_INFO_OUTPUT_BUFFER_VERSION
#define REFS_QUERY_VOLUME_IO_METRICS_INFO_OUTPUT_BUFFER_VERSION 1
#endif
#ifndef REFS_QUERY_VOLUME_TOTAL_SHARED_LCNS_OUTPUT_BUFFER_VERSION
#define REFS_QUERY_VOLUME_TOTAL_SHARED_LCNS_OUTPUT_BUFFER_VERSION 1
#endif
#ifndef REFS_SET_ROLLBACK_PROTECTION_INFO_INPUT_BUFFER_VERSION
#define REFS_SET_ROLLBACK_PROTECTION_INFO_INPUT_BUFFER_VERSION 2
#endif
#ifndef REFS_SET_ROLLBACK_PROTECTION_INFO_INPUT_BUFFER_VERSION_V1
#define REFS_SET_ROLLBACK_PROTECTION_INFO_INPUT_BUFFER_VERSION_V1 1
#endif
#ifndef REFS_SET_VOLUME_COMPRESSION_INFO_INPUT_BUFFER_VERSION
#define REFS_SET_VOLUME_COMPRESSION_INFO_INPUT_BUFFER_VERSION 1
#endif
#ifndef REFS_SET_VOLUME_IO_METRICS_INFO_INPUT_BUFFER_VERSION
#define REFS_SET_VOLUME_IO_METRICS_INFO_INPUT_BUFFER_VERSION 1
#endif
#ifndef REFS_SMR_VOLUME_GC_PARAMETERS_VERSION_V1
#define REFS_SMR_VOLUME_GC_PARAMETERS_VERSION_V1 1
#endif
#ifndef REFS_SMR_VOLUME_INFO_OUTPUT_VERSION_V0
#define REFS_SMR_VOLUME_INFO_OUTPUT_VERSION_V0 0
#endif
#ifndef REFS_SMR_VOLUME_INFO_OUTPUT_VERSION_V1
#define REFS_SMR_VOLUME_INFO_OUTPUT_VERSION_V1 1
#endif
#ifndef REFS_VOLUME_DEDUP_INFO_INPUT_BUFFER_VERSION
#define REFS_VOLUME_DEDUP_INFO_INPUT_BUFFER_VERSION 2
#endif
#ifndef REFS_VOLUME_DEDUP_INFO_INPUT_BUFFER_VERSION_V1
#define REFS_VOLUME_DEDUP_INFO_INPUT_BUFFER_VERSION_V1 1
#endif
#ifndef REFS_VOLUME_DEDUP_INFO_OUTPUT_BUFFER_VERSION
#define REFS_VOLUME_DEDUP_INFO_OUTPUT_BUFFER_VERSION 2
#endif
#ifndef REFS_VOLUME_DEDUP_INFO_OUTPUT_BUFFER_VERSION_V1
#define REFS_VOLUME_DEDUP_INFO_OUTPUT_BUFFER_VERSION_V1 1
#endif
#ifndef REMOTE_PROTOCOL_FLAG_INTEGRITY
#define REMOTE_PROTOCOL_FLAG_INTEGRITY 0x00000010
#endif
#ifndef REMOTE_PROTOCOL_FLAG_MUTUAL_AUTH
#define REMOTE_PROTOCOL_FLAG_MUTUAL_AUTH 0x00000020
#endif
#ifndef REMOTE_PROTOCOL_FLAG_PERSISTENT_HANDLE
#define REMOTE_PROTOCOL_FLAG_PERSISTENT_HANDLE 0x00000004
#endif
#ifndef REMOTE_PROTOCOL_FLAG_PRIVACY
#define REMOTE_PROTOCOL_FLAG_PRIVACY 0x00000008
#endif
#ifndef REPARSE_DATA_EX_FLAG_GIVEN_TAG_OR_NONE
#define REPARSE_DATA_EX_FLAG_GIVEN_TAG_OR_NONE 0x00000001
#endif
#ifndef REQUEST_OPLOCK_OUTPUT_FLAG_WRITABLE_SECTION_PRESENT
#define REQUEST_OPLOCK_OUTPUT_FLAG_WRITABLE_SECTION_PRESENT 0x00000004
#endif
#ifndef RPI_SMB2_SERVERCAP_DFS
#define RPI_SMB2_SERVERCAP_DFS 0x00000001
#endif
#ifndef RPI_SMB2_SERVERCAP_DIRECTORY_LEASING
#define RPI_SMB2_SERVERCAP_DIRECTORY_LEASING 0x00000020
#endif
#ifndef RPI_SMB2_SERVERCAP_ENCRYPTION_AWARE
#define RPI_SMB2_SERVERCAP_ENCRYPTION_AWARE 0x00000040
#endif
#ifndef RPI_SMB2_SERVERCAP_LARGEMTU
#define RPI_SMB2_SERVERCAP_LARGEMTU 0x00000004
#endif
#ifndef RPI_SMB2_SERVERCAP_LEASING
#define RPI_SMB2_SERVERCAP_LEASING 0x00000002
#endif
#ifndef RPI_SMB2_SERVERCAP_MULTICHANNEL
#define RPI_SMB2_SERVERCAP_MULTICHANNEL 0x00000008
#endif
#ifndef RPI_SMB2_SERVERCAP_NOTIFICATIONS_AWARE
#define RPI_SMB2_SERVERCAP_NOTIFICATIONS_AWARE 0x00000080
#endif
#ifndef RPI_SMB2_SERVERCAP_PERSISTENT_HANDLES
#define RPI_SMB2_SERVERCAP_PERSISTENT_HANDLES 0x00000010
#endif
#ifndef RPI_SMB2_SHARECAP_ACCESS_BASED_DIRECTORY_ENUM
#define RPI_SMB2_SHARECAP_ACCESS_BASED_DIRECTORY_ENUM 0x00000100
#endif
#ifndef RPI_SMB2_SHARECAP_ASYMMETRIC_SCALEOUT
#define RPI_SMB2_SHARECAP_ASYMMETRIC_SCALEOUT 0x00000400
#endif
#ifndef RPI_SMB2_SHARECAP_CLUSTER
#define RPI_SMB2_SHARECAP_CLUSTER 0x00000040
#endif
#ifndef RPI_SMB2_SHARECAP_CONTINUOUS_AVAILABILITY
#define RPI_SMB2_SHARECAP_CONTINUOUS_AVAILABILITY 0x00000010
#endif
#ifndef RPI_SMB2_SHARECAP_DFS
#define RPI_SMB2_SHARECAP_DFS 0x00000008
#endif
#ifndef RPI_SMB2_SHARECAP_ENCRYPTED
#define RPI_SMB2_SHARECAP_ENCRYPTED 0x00000080
#endif
#ifndef RPI_SMB2_SHARECAP_IDENTITY_REMOTING
#define RPI_SMB2_SHARECAP_IDENTITY_REMOTING 0x00000200
#endif
#ifndef RPI_SMB2_SHARECAP_SCALEOUT
#define RPI_SMB2_SHARECAP_SCALEOUT 0x00000020
#endif
#ifndef RPI_SMB2_SHARECAP_TIMEWARP
#define RPI_SMB2_SHARECAP_TIMEWARP 0x00000002
#endif
#ifndef RPI_SMB2_SHAREFLAG_COMPRESS_DATA
#define RPI_SMB2_SHAREFLAG_COMPRESS_DATA 0x00000002
#endif
#ifndef RPI_SMB2_SHAREFLAG_ENCRYPT_DATA
#define RPI_SMB2_SHAREFLAG_ENCRYPT_DATA 0x00000001
#endif
#ifndef RPI_SMB2_SHARETYPE_DISK
#define RPI_SMB2_SHARETYPE_DISK 0
#endif
#ifndef RPI_SMB2_SHARETYPE_PIPE
#define RPI_SMB2_SHARETYPE_PIPE 1
#endif
#ifndef RPI_SMB2_SHARETYPE_PRINT
#define RPI_SMB2_SHARETYPE_PRINT 2
#endif
#ifndef RTL_HEAP_MEMORY_LIMIT_CURRENT_VERSION
#define RTL_HEAP_MEMORY_LIMIT_CURRENT_VERSION 1
#endif
#ifndef SCRUB_DATA_INPUT_FLAG_IGNORE_REDUNDANCY
#define SCRUB_DATA_INPUT_FLAG_IGNORE_REDUNDANCY 0x00000008
#endif
#ifndef SCRUB_DATA_INPUT_FLAG_OPLOCK_NOT_ACQUIRED
#define SCRUB_DATA_INPUT_FLAG_OPLOCK_NOT_ACQUIRED 0x00000040
#endif
#ifndef SCRUB_DATA_INPUT_FLAG_RESUME
#define SCRUB_DATA_INPUT_FLAG_RESUME 0x00000001
#endif
#ifndef SCRUB_DATA_INPUT_FLAG_SCRUB_BY_OBJECT_ID
#define SCRUB_DATA_INPUT_FLAG_SCRUB_BY_OBJECT_ID 0x00000020
#endif
#ifndef SCRUB_DATA_INPUT_FLAG_SKIP_DATA
#define SCRUB_DATA_INPUT_FLAG_SKIP_DATA 0x00000010
#endif
#ifndef SCRUB_DATA_INPUT_FLAG_SKIP_IN_SYNC
#define SCRUB_DATA_INPUT_FLAG_SKIP_IN_SYNC 0x00000002
#endif
#ifndef SCRUB_DATA_INPUT_FLAG_SKIP_NON_INTEGRITY_DATA
#define SCRUB_DATA_INPUT_FLAG_SKIP_NON_INTEGRITY_DATA 0x00000004
#endif
#ifndef SCRUB_DATA_OUTPUT_FLAG_INCOMPLETE
#define SCRUB_DATA_OUTPUT_FLAG_INCOMPLETE 0x00000001
#endif
#ifndef SCRUB_DATA_OUTPUT_FLAG_NON_USER_DATA_RANGE
#define SCRUB_DATA_OUTPUT_FLAG_NON_USER_DATA_RANGE 0x00010000
#endif
#ifndef SCRUB_DATA_OUTPUT_FLAG_PARITY_EXTENT_DATA_RETURNED
#define SCRUB_DATA_OUTPUT_FLAG_PARITY_EXTENT_DATA_RETURNED 0x00020000
#endif
#ifndef SCRUB_DATA_OUTPUT_FLAG_RESUME_CONTEXT_LENGTH_SPECIFIED
#define SCRUB_DATA_OUTPUT_FLAG_RESUME_CONTEXT_LENGTH_SPECIFIED 0x00040000
#endif
#ifndef SECBUFFER_APP_SESSION_STATE
#define SECBUFFER_APP_SESSION_STATE 31
#endif
#ifndef SECBUFFER_CERTIFICATE_REQUEST_CONTEXT
#define SECBUFFER_CERTIFICATE_REQUEST_CONTEXT 29
#endif
#ifndef SECBUFFER_CHANNEL_BINDINGS_RESULT
#define SECBUFFER_CHANNEL_BINDINGS_RESULT 30
#endif
#ifndef SECBUFFER_FLAGS
#define SECBUFFER_FLAGS 27
#endif
#ifndef SECBUFFER_PRESHARED_KEY
#define SECBUFFER_PRESHARED_KEY 22
#endif
#ifndef SECBUFFER_PRESHARED_KEY_IDENTITY
#define SECBUFFER_PRESHARED_KEY_IDENTITY 23
#endif
#ifndef SECBUFFER_SEND_GENERIC_TLS_EXTENSION
#define SECBUFFER_SEND_GENERIC_TLS_EXTENSION 25
#endif
#ifndef SECBUFFER_SESSION_TICKET
#define SECBUFFER_SESSION_TICKET 32
#endif
#ifndef SECBUFFER_SRTP_MASTER_KEY_IDENTIFIER
#define SECBUFFER_SRTP_MASTER_KEY_IDENTIFIER 20
#endif
#ifndef SECBUFFER_SRTP_PROTECTION_PROFILES
#define SECBUFFER_SRTP_PROTECTION_PROFILES 19
#endif
#ifndef SECBUFFER_SUBSCRIBE_GENERIC_TLS_EXTENSION
#define SECBUFFER_SUBSCRIBE_GENERIC_TLS_EXTENSION 26
#endif
#ifndef SECBUFFER_TOKEN_BINDING
#define SECBUFFER_TOKEN_BINDING 21
#endif
#ifndef SECBUFFER_TRAFFIC_SECRETS
#define SECBUFFER_TRAFFIC_SECRETS 28
#endif
#ifndef SECPKG_ATTR_DTLS_MTU
#define SECPKG_ATTR_DTLS_MTU 34
#endif
#ifndef SECPKG_ATTR_IS_LOOPBACK
#define SECPKG_ATTR_IS_LOOPBACK 37
#endif
#ifndef SECPKG_ATTR_NEGOTIATED_TLS_EXTENSIONS
#define SECPKG_ATTR_NEGOTIATED_TLS_EXTENSIONS 36
#endif
#ifndef SECPKG_CALLFLAGS_APPCONTAINER
#define SECPKG_CALLFLAGS_APPCONTAINER 0x00000001
#endif
#ifndef SECPKG_CALLFLAGS_APPCONTAINER_AUTHCAPABLE
#define SECPKG_CALLFLAGS_APPCONTAINER_AUTHCAPABLE 0x00000002
#endif
#ifndef SECPKG_CALLFLAGS_APPCONTAINER_UPNCAPABLE
#define SECPKG_CALLFLAGS_APPCONTAINER_UPNCAPABLE 0x00000008
#endif
#ifndef SECPKG_CALLFLAGS_FORCE_SUPPLIED
#define SECPKG_CALLFLAGS_FORCE_SUPPLIED 0x00000004
#endif
#ifndef SECPKG_CRED_ATTR_CERT
#define SECPKG_CRED_ATTR_CERT 4
#endif
#ifndef SECPKG_CRED_ATTR_KDC_NETWORK_SETTINGS
#define SECPKG_CRED_ATTR_KDC_NETWORK_SETTINGS 3
#endif
#ifndef SECPKG_CRED_ATTR_KDC_PROXY_SETTINGS
#define SECPKG_CRED_ATTR_KDC_PROXY_SETTINGS 3
#endif
#ifndef SECPKG_CRED_ATTR_PAC_BYPASS
#define SECPKG_CRED_ATTR_PAC_BYPASS 5
#endif
#ifndef SECPKG_CRED_KERB_ANCHOR_DS_VERSION
#define SECPKG_CRED_KERB_ANCHOR_DS_VERSION 0x00000040
#endif
#ifndef SECURITY_AUTHENTICATION_AUTHORITY_ASSERTED_RID
#define SECURITY_AUTHENTICATION_AUTHORITY_ASSERTED_RID 0x00000001L
#endif
#ifndef SECURITY_AUTHENTICATION_FRESH_KEY_AUTH_RID
#define SECURITY_AUTHENTICATION_FRESH_KEY_AUTH_RID 0x00000003L
#endif
#ifndef SECURITY_AUTHENTICATION_KEY_PROPERTY_ATTESTATION_RID
#define SECURITY_AUTHENTICATION_KEY_PROPERTY_ATTESTATION_RID 0x00000006L
#endif
#ifndef SECURITY_AUTHENTICATION_KEY_PROPERTY_MFA_RID
#define SECURITY_AUTHENTICATION_KEY_PROPERTY_MFA_RID 0x00000005L
#endif
#ifndef SECURITY_AUTHENTICATION_KEY_TRUST_RID
#define SECURITY_AUTHENTICATION_KEY_TRUST_RID 0x00000004L
#endif
#ifndef SECURITY_AUTHENTICATION_SERVICE_ASSERTED_RID
#define SECURITY_AUTHENTICATION_SERVICE_ASSERTED_RID 0x00000002L
#endif
#ifndef SECURITY_CAPABILITY_APP_SILO_RID
#define SECURITY_CAPABILITY_APP_SILO_RID 0x00010000L
#endif
#ifndef SECURITY_DESCRIPTOR_DO_NOT_FREE
#define SECURITY_DESCRIPTOR_DO_NOT_FREE 0x04000000
#endif
#ifndef SECURITY_EDGE_CLOUD_INFRASTRUCTURE_SERVICE_ID_BASE_RID
#define SECURITY_EDGE_CLOUD_INFRASTRUCTURE_SERVICE_ID_BASE_RID 0x00000062L
#endif
#ifndef SECURITY_INSTALLER_CAPABILITY_RID_COUNT
#define SECURITY_INSTALLER_CAPABILITY_RID_COUNT 10
#endif
#ifndef SECURITY_INSTALLER_GROUP_CAPABILITY_BASE
#define SECURITY_INSTALLER_GROUP_CAPABILITY_BASE 0x20
#endif
#ifndef SECURITY_INSTALLER_GROUP_CAPABILITY_RID_COUNT
#define SECURITY_INSTALLER_GROUP_CAPABILITY_RID_COUNT 9
#endif
#ifndef SECURITY_LOCAL_ACCOUNT_AND_ADMIN_RID
#define SECURITY_LOCAL_ACCOUNT_AND_ADMIN_RID 0x00000072L
#endif
#ifndef SECURITY_LOCAL_ACCOUNT_RID
#define SECURITY_LOCAL_ACCOUNT_RID 0x00000071L
#endif
#ifndef SECURITY_PROCESS_PROTECTION_LEVEL_ANTIMALWARE_RID
#define SECURITY_PROCESS_PROTECTION_LEVEL_ANTIMALWARE_RID 0x00000600L
#endif
#ifndef SECURITY_RESTRICTED_SERVICES_BASE_RID
#define SECURITY_RESTRICTED_SERVICES_BASE_RID 0x00000063L
#endif
#ifndef SECURITY_RESTRICTED_SERVICES_RID_COUNT
#define SECURITY_RESTRICTED_SERVICES_RID_COUNT 6L
#endif
#ifndef SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION_5
#define SECURITY_SUPPORT_PROVIDER_INTERFACE_VERSION_5 5
#endif
#ifndef SECURITY_UNIQUIFIED_SERVICE_BASE_RID
#define SECURITY_UNIQUIFIED_SERVICE_BASE_RID 0x00000061L
#endif
#ifndef SEC_CHANNEL_BINDINGS_AUDIT_BINDINGS
#define SEC_CHANNEL_BINDINGS_AUDIT_BINDINGS 0x1
#endif
#ifndef SEC_CHANNEL_BINDINGS_RESULT_ABSENT
#define SEC_CHANNEL_BINDINGS_RESULT_ABSENT 0x2
#endif
#ifndef SEC_CHANNEL_BINDINGS_RESULT_CLIENT_SUPPORT
#define SEC_CHANNEL_BINDINGS_RESULT_CLIENT_SUPPORT 0x1
#endif
#ifndef SEC_CHANNEL_BINDINGS_RESULT_NOTVALID_MISMATCH
#define SEC_CHANNEL_BINDINGS_RESULT_NOTVALID_MISMATCH 0x4
#endif
#ifndef SEC_CHANNEL_BINDINGS_RESULT_NOTVALID_MISSING
#define SEC_CHANNEL_BINDINGS_RESULT_NOTVALID_MISSING 0x8
#endif
#ifndef SEC_CHANNEL_BINDINGS_RESULT_VALID_MATCHED
#define SEC_CHANNEL_BINDINGS_RESULT_VALID_MATCHED 0x10
#endif
#ifndef SEC_CHANNEL_BINDINGS_RESULT_VALID_MISSING
#define SEC_CHANNEL_BINDINGS_RESULT_VALID_MISSING 0x40
#endif
#ifndef SEC_CHANNEL_BINDINGS_RESULT_VALID_PROXY
#define SEC_CHANNEL_BINDINGS_RESULT_VALID_PROXY 0x20
#endif
#ifndef SEF_FORCE_USER_MODE
#define SEF_FORCE_USER_MODE 0x2000
#endif
#ifndef SEF_NORMALIZE_OUTPUT_DESCRIPTOR
#define SEF_NORMALIZE_OUTPUT_DESCRIPTOR 0x4000
#endif
#ifndef SEGMENT_HEAP_FLG_NO_LFH
#define SEGMENT_HEAP_FLG_NO_LFH 0x2
#endif
#ifndef SEGMENT_HEAP_FLG_USE_PAGE_HEAP
#define SEGMENT_HEAP_FLG_USE_PAGE_HEAP 0x1
#endif
#ifndef SEGMENT_HEAP_PARAMETERS_VERSION
#define SEGMENT_HEAP_PARAMETERS_VERSION 3
#endif
#ifndef SEGMENT_HEAP_PARAMS_VALID_FLAGS
#define SEGMENT_HEAP_PARAMS_VALID_FLAGS 0x3
#endif
#ifndef SE_ACCESS_CHECK_FLAG_NO_LEARNING_MODE_LOGGING
#define SE_ACCESS_CHECK_FLAG_NO_LEARNING_MODE_LOGGING 0x00000008
#endif
#ifndef SE_ACCESS_CHECK_VALID_FLAGS
#define SE_ACCESS_CHECK_VALID_FLAGS 0x00000008
#endif
#ifndef SHUFFLE_FILE_FLAG_SKIP_INITIALIZING_NEW_CLUSTERS
#define SHUFFLE_FILE_FLAG_SKIP_INITIALIZING_NEW_CLUSTERS 0x00000001
#endif
#ifndef SPACES_TRACKED_OFFSET_HEADER_FLAG
#define SPACES_TRACKED_OFFSET_HEADER_FLAG 0x0002
#endif
#ifndef STREAMS_ASSOCIATE_ID_CLEAR
#define STREAMS_ASSOCIATE_ID_CLEAR 0x1
#endif
#ifndef STREAMS_ASSOCIATE_ID_SET
#define STREAMS_ASSOCIATE_ID_SET 0x2
#endif
#ifndef STREAMS_INVALID_ID
#define STREAMS_INVALID_ID 0
#endif
#ifndef STREAM_EXTENT_ENTRY_ALL_EXTENTS
#define STREAM_EXTENT_ENTRY_ALL_EXTENTS 0x00000002
#endif
#ifndef STREAM_EXTENT_ENTRY_AS_RETRIEVAL_POINTERS
#define STREAM_EXTENT_ENTRY_AS_RETRIEVAL_POINTERS 0x00000001
#endif
#ifndef STREAM_LAYOUT_ENTRY_HAS_INFORMATION
#define STREAM_LAYOUT_ENTRY_HAS_INFORMATION 0x00000010
#endif
#ifndef STREAM_LAYOUT_ENTRY_IMMOVABLE
#define STREAM_LAYOUT_ENTRY_IMMOVABLE 0x00000001
#endif
#ifndef STREAM_LAYOUT_ENTRY_NO_CLUSTERS_ALLOCATED
#define STREAM_LAYOUT_ENTRY_NO_CLUSTERS_ALLOCATED 0x00000008
#endif
#ifndef STREAM_LAYOUT_ENTRY_PINNED
#define STREAM_LAYOUT_ENTRY_PINNED 0x00000002
#endif
#ifndef STREAM_LAYOUT_ENTRY_RESIDENT
#define STREAM_LAYOUT_ENTRY_RESIDENT 0x00000004
#endif
#ifndef SUPPORTED_FS_FEATURES_BYPASS_IO
#define SUPPORTED_FS_FEATURES_BYPASS_IO 0x00000008
#endif
#ifndef SUPPORTED_FS_FEATURES_OFFLOAD_READ
#define SUPPORTED_FS_FEATURES_OFFLOAD_READ 0x00000001
#endif
#ifndef SUPPORTED_FS_FEATURES_OFFLOAD_WRITE
#define SUPPORTED_FS_FEATURES_OFFLOAD_WRITE 0x00000002
#endif
#ifndef SUPPORTED_FS_FEATURES_QUERY_OPEN
#define SUPPORTED_FS_FEATURES_QUERY_OPEN 0x00000004
#endif
#ifndef SYMLINK_DIRECTORY
#define SYMLINK_DIRECTORY 0x80000000
#endif
#ifndef SYMLINK_FILE
#define SYMLINK_FILE 0x40000000
#endif
#ifndef SYMLINK_RESERVED_MASK
#define SYMLINK_RESERVED_MASK 0xF0000000
#endif
#ifndef SYSTEM_ACCESS_FILTER_NOCONSTRAINT_MASK
#define SYSTEM_ACCESS_FILTER_NOCONSTRAINT_MASK 0xffffffff
#endif
#ifndef SYSTEM_PROCESS_TRUST_NOCONSTRAINT_MASK
#define SYSTEM_PROCESS_TRUST_NOCONSTRAINT_MASK 0xffffffff
#endif
#ifndef TOKEN_AUDIT_REDIRECTION_TRUST
#define TOKEN_AUDIT_REDIRECTION_TRUST 0x00800000
#endif
#ifndef TOKEN_ENFORCE_REDIRECTION_TRUST
#define TOKEN_ENFORCE_REDIRECTION_TRUST 0x00400000
#endif
#ifndef TOKEN_LEARNING_MODE_LOGGING
#define TOKEN_LEARNING_MODE_LOGGING 0x01000000
#endif
#ifndef TOKEN_PERMISSIVE_LEARNING_MODE
#define TOKEN_PERMISSIVE_LEARNING_MODE 0x03000000
#endif
#ifndef TOKEN_SYSTEM_MANAGED_ADMIN_FULL_TOKEN
#define TOKEN_SYSTEM_MANAGED_ADMIN_FULL_TOKEN 0x08000000
#endif
#ifndef TRUST_PROTECTED_FILTER_ACE_FLAG
#define TRUST_PROTECTED_FILTER_ACE_FLAG 0x40
#endif
#ifndef UNDERSTANDS_LONG_NAMES
#define UNDERSTANDS_LONG_NAMES 1
#endif
#ifndef USN_REASON_DESIRED_STORAGE_CLASS_CHANGE
#define USN_REASON_DESIRED_STORAGE_CLASS_CHANGE 0x01000000
#endif
#ifndef USN_REASON_INTEGRITY_CHANGE
#define USN_REASON_INTEGRITY_CHANGE 0x00800000
#endif
#ifndef USN_SOURCE_CLIENT_REPLICATION_MANAGEMENT
#define USN_SOURCE_CLIENT_REPLICATION_MANAGEMENT 0x00000008
#endif
#ifndef VM_PREFETCH_TO_WORKING_SET
#define VM_PREFETCH_TO_WORKING_SET 0x1
#endif
#ifndef WIM_BOOT_NOT_OS_WIM
#define WIM_BOOT_NOT_OS_WIM 0x00000000
#endif
#ifndef WIM_BOOT_OS_WIM
#define WIM_BOOT_OS_WIM 0x00000001
#endif
#ifndef WIM_PROVIDER_CURRENT_VERSION
#define WIM_PROVIDER_CURRENT_VERSION 0x00000001
#endif
#ifndef WIM_PROVIDER_EXTERNAL_FLAG_NOT_ACTIVE
#define WIM_PROVIDER_EXTERNAL_FLAG_NOT_ACTIVE 0x00000001
#endif
#ifndef WIM_PROVIDER_EXTERNAL_FLAG_SUSPENDED
#define WIM_PROVIDER_EXTERNAL_FLAG_SUSPENDED 0x00000002
#endif
#ifndef WIM_PROVIDER_HASH_SIZE
#define WIM_PROVIDER_HASH_SIZE 20
#endif
#ifndef WNNC_NET_9P
#define WNNC_NET_9P 0x00480000
#endif
#ifndef WNNC_NET_AURISTOR_FS
#define WNNC_NET_AURISTOR_FS 0x00460000
#endif
#ifndef WNNC_NET_AVID1
#define WNNC_NET_AVID1 0X003A0000
#endif
#ifndef WNNC_NET_DAV
#define WNNC_NET_DAV 0x002E0000
#endif
#ifndef WNNC_NET_DFS
#define WNNC_NET_DFS 0x003B0000
#endif
#ifndef WNNC_NET_DOCUSHARE
#define WNNC_NET_DOCUSHARE 0x00450000
#endif
#ifndef WNNC_NET_DRIVEONWEB
#define WNNC_NET_DRIVEONWEB 0x003E0000
#endif
#ifndef WNNC_NET_EXIFS
#define WNNC_NET_EXIFS 0x002D0000
#endif
#ifndef WNNC_NET_FOXBAT
#define WNNC_NET_FOXBAT 0x002B0000
#endif
#ifndef WNNC_NET_GOOGLE
#define WNNC_NET_GOOGLE 0x00430000
#endif
#ifndef WNNC_NET_KNOWARE
#define WNNC_NET_KNOWARE 0x002F0000
#endif
#ifndef WNNC_NET_KWNP
#define WNNC_NET_KWNP 0x003C0000
#endif
#ifndef WNNC_NET_LOCK
#define WNNC_NET_LOCK 0x00350000
#endif
#ifndef WNNC_NET_MFILES
#define WNNC_NET_MFILES 0x00410000
#endif
#ifndef WNNC_NET_MS_NFS
#define WNNC_NET_MS_NFS 0x00420000
#endif
#ifndef WNNC_NET_NDFS
#define WNNC_NET_NDFS 0x00440000
#endif
#ifndef WNNC_NET_OPENAFS
#define WNNC_NET_OPENAFS 0x00390000
#endif
#ifndef WNNC_NET_QUINCY
#define WNNC_NET_QUINCY 0x00380000
#endif
#ifndef WNNC_NET_RIVERFRONT1
#define WNNC_NET_RIVERFRONT1 0X001E0000
#endif
#ifndef WNNC_NET_RIVERFRONT2
#define WNNC_NET_RIVERFRONT2 0x001F0000
#endif
#ifndef WNNC_NET_RSFX
#define WNNC_NET_RSFX 0x00400000
#endif
#ifndef WNNC_NET_SECUREAGENT
#define WNNC_NET_SECUREAGENT 0x00470000
#endif
#ifndef WNNC_NET_SMB
#define WNNC_NET_SMB 0x00020000
#endif
#ifndef WNNC_NET_SRT
#define WNNC_NET_SRT 0x00370000
#endif
#ifndef WNNC_NET_STAC
#define WNNC_NET_STAC 0x002A0000
#endif
#ifndef WNNC_NET_TERMSRV
#define WNNC_NET_TERMSRV 0x00360000
#endif
#ifndef WNNC_NET_VMWARE
#define WNNC_NET_VMWARE 0x003F0000
#endif
#ifndef WNNC_NET_YAHOO
#define WNNC_NET_YAHOO 0x002C0000
#endif
#ifndef WNNC_NET_ZENWORKS
#define WNNC_NET_ZENWORKS 0x003D0000
#endif
#ifndef WOF_CURRENT_VERSION
#define WOF_CURRENT_VERSION 0x00000001
#endif
#ifndef WOF_PROVIDER_CLOUD
#define WOF_PROVIDER_CLOUD 0x00000003
#endif
#ifndef WOF_PROVIDER_FILE
#define WOF_PROVIDER_FILE 0x00000002
#endif
#ifndef WOF_PROVIDER_WIM
#define WOF_PROVIDER_WIM 0x00000001
typedef enum _RTL_MEMORY_TYPE {
    MemoryTypePaged = 0,
    MemoryTypeNonPaged = 1,
    MemoryType64KPage = 2,
    MemoryTypeLargePage = 3,
    MemoryTypeHugePage = 4,
    MemoryTypeCustom = 5,
    MemoryTypeMax = 6
} RTL_MEMORY_TYPE, *PRTL_MEMORY_TYPE;

typedef enum _HEAP_MEMORY_INFO_CLASS {
    HeapMemoryBasicInformation = 0
} HEAP_MEMORY_INFO_CLASS;

typedef enum _MSV1_0_CREDENTIAL_KEY_TYPE {
    InvalidCredKey = 0,
    DeprecatedIUMCredKey = 1,
    DomainUserCredKey = 2,
    LocalUserCredKey = 3,
    ExternallySuppliedCredKey = 4
} MSV1_0_CREDENTIAL_KEY_TYPE;

typedef enum _FILE_KNOWN_FOLDER_TYPE {
    KnownFolderNone = 0,
    KnownFolderDesktop = 1,
    KnownFolderDocuments = 2,
    KnownFolderDownloads = 3,
    KnownFolderMusic = 4,
    KnownFolderPictures = 5,
    KnownFolderVideos = 6,
    KnownFolderOther = 7,
    KnownFolderMax = 7
} FILE_KNOWN_FOLDER_TYPE;

typedef enum _CSVFS_DISK_CONNECTIVITY {
    CsvFsDiskConnectivityNone = 0,
    CsvFsDiskConnectivityMdsNodeOnly = 1,
    CsvFsDiskConnectivitySubsetOfNodes = 2,
    CsvFsDiskConnectivityAllNodes = 3
} CSVFS_DISK_CONNECTIVITY, *PCSVFS_DISK_CONNECTIVITY;

typedef enum _LMR_QUERY_INFO_CLASS {
    LMRQuerySessionInfo = 1
} LMR_QUERY_INFO_CLASS, *PLMR_QUERY_INFO_CLASS;

typedef enum _STORAGE_RESERVE_ID {
    StorageReserveIdNone = 0,
    StorageReserveIdHard = 1,
    StorageReserveIdSoft = 2,
    StorageReserveIdUpdateScratch = 3,
    StorageReserveIdMax = 4
} STORAGE_RESERVE_ID, *PSTORAGE_RESERVE_ID;

typedef enum _QUERY_FILE_LAYOUT_FILTER_TYPE {
    QUERY_FILE_LAYOUT_FILTER_TYPE_NONE = 0,
    QUERY_FILE_LAYOUT_FILTER_TYPE_CLUSTERS = 1,
    QUERY_FILE_LAYOUT_FILTER_TYPE_FILEID = 2,
    QUERY_FILE_LAYOUT_FILTER_TYPE_STORAGE_RESERVE_ID = 3,
    QUERY_FILE_LAYOUT_NUM_FILTER_TYPES = 4
} QUERY_FILE_LAYOUT_FILTER_TYPE;

typedef enum _FILE_STORAGE_TIER_MEDIA_TYPE {
    FileStorageTierMediaTypeUnspecified = 0,
    FileStorageTierMediaTypeDisk = 1,
    FileStorageTierMediaTypeSsd = 2,
    FileStorageTierMediaTypeScm = 4,
    FileStorageTierMediaTypeMax = 5
} FILE_STORAGE_TIER_MEDIA_TYPE, *PFILE_STORAGE_TIER_MEDIA_TYPE;

typedef enum _FILE_STORAGE_TIER_CLASS {
    FileStorageTierClassUnspecified = 0,
    FileStorageTierClassCapacity = 1,
    FileStorageTierClassPerformance = 2,
    FileStorageTierClassMax = 3
} FILE_STORAGE_TIER_CLASS, *PFILE_STORAGE_TIER_CLASS;

typedef enum _DUPLICATE_EXTENTS_STATE {
    FileSnapStateInactive = 0,
    FileSnapStateSource = 1,
    FileSnapStateTarget = 2
} DUPLICATE_EXTENTS_STATE, *PDUPLICATE_EXTENTS_STATE;

typedef enum _REFS_SMR_VOLUME_GC_STATE {
    SmrGcStateInactive = 0,
    SmrGcStatePaused = 1,
    SmrGcStateActive = 2,
    SmrGcStateActiveFullSpeed = 3
} REFS_SMR_VOLUME_GC_STATE, *PREFS_SMR_VOLUME_GC_STATE;

typedef enum _REFS_SMR_VOLUME_GC_ACTION {
    SmrGcActionStart = 1,
    SmrGcActionStartFullSpeed = 2,
    SmrGcActionPause = 3,
    SmrGcActionStop = 4
} REFS_SMR_VOLUME_GC_ACTION, *PREFS_SMR_VOLUME_GC_ACTION;

typedef enum _REFS_SMR_VOLUME_GC_METHOD {
    SmrGcMethodCompaction = 1,
    SmrGcMethodCompression = 2,
    SmrGcMethodRotation = 3
} REFS_SMR_VOLUME_GC_METHOD, *PREFS_SMR_VOLUME_GC_METHOD;

typedef enum _VIRTUAL_STORAGE_BEHAVIOR_CODE {
    VirtualStorageBehaviorUndefined = 0,
    VirtualStorageBehaviorCacheWriteThrough = 1,
    VirtualStorageBehaviorCacheWriteBack = 2,
    VirtualStorageBehaviorStopIoProcessing = 3,
    VirtualStorageBehaviorRestartIoProcessing = 4
} VIRTUAL_STORAGE_BEHAVIOR_CODE, *PVIRTUAL_STORAGE_BEHAVIOR_CODE;

typedef enum _FS_BPIO_OPERATIONS {
    FS_BPIO_OP_ENABLE = 1,
    FS_BPIO_OP_DISABLE = 2,
    FS_BPIO_OP_QUERY = 3,
    FS_BPIO_OP_VOLUME_STACK_PAUSE = 4,
    FS_BPIO_OP_VOLUME_STACK_RESUME = 5,
    FS_BPIO_OP_STREAM_PAUSE = 6,
    FS_BPIO_OP_STREAM_RESUME = 7,
    FS_BPIO_OP_GET_INFO = 8,
    FS_BPIO_OP_MAX_OPERATION = 9
} FS_BPIO_OPERATIONS;

typedef enum _FS_BPIO_INFLAGS {
    FSBPIO_INFL_None = 0,
    FSBPIO_INFL_SKIP_STORAGE_STACK_QUERY = 1
} FS_BPIO_INFLAGS;

typedef enum _FS_BPIO_OUTFLAGS {
    FSBPIO_OUTFL_None = 0,
    FSBPIO_OUTFL_VOLUME_STACK_BYPASS_PAUSED = 1,
    FSBPIO_OUTFL_STREAM_BYPASS_PAUSED = 2,
    FSBPIO_OUTFL_FILTER_ATTACH_BLOCKED = 4,
    FSBPIO_OUTFL_COMPATIBLE_STORAGE_DRIVER = 8
} FS_BPIO_OUTFLAGS;

typedef enum _SharedVirtualDiskSupportType {
    SharedVirtualDisksUnsupported = 0,
    SharedVirtualDisksSupported = 1,
    SharedVirtualDiskSnapshotsSupported = 3,
    SharedVirtualDiskCDPSnapshotsSupported = 7
} SharedVirtualDiskSupportType;

typedef enum _SharedVirtualDiskHandleState {
    SharedVirtualDiskHandleStateNone = 0,
    SharedVirtualDiskHandleStateFileShared = 1,
    SharedVirtualDiskHandleStateHandleShared = 3
} SharedVirtualDiskHandleState;

typedef enum _REFS_DEALLOCATE_RANGES_ALLOCATOR {
    REFS_DEALLOCATE_RANGES_ALLOCATOR_NONE = 0,
    REFS_DEALLOCATE_RANGES_ALLOCATOR_SAA = 1,
    REFS_DEALLOCATE_RANGES_ALLOCATOR_CAA = 2,
    REFS_DEALLOCATE_RANGES_ALLOCATOR_MAA = 3
} REFS_DEALLOCATE_RANGES_ALLOCATOR, *PREFS_DEALLOCATE_RANGES_ALLOCATOR;

enum _REFS_STREAM_EXTENT_PROPERTIES {
    REFS_STREAM_EXTENT_PROPERTY_VALID = 16,
    REFS_STREAM_EXTENT_PROPERTY_STREAM_RESERVED = 32,
    REFS_STREAM_EXTENT_PROPERTY_CRC32 = 128,
    REFS_STREAM_EXTENT_PROPERTY_CRC64 = 0x100,
    REFS_STREAM_EXTENT_PROPERTY_GHOSTED = 0x200,
    REFS_STREAM_EXTENT_PROPERTY_READONLY = 0x400,
    REFS_STREAM_EXTENT_PROPERTY_SPARSE = 8
};

typedef enum _REFS_STREAM_SNAPSHOT_OPERATION {
    REFS_STREAM_SNAPSHOT_OPERATION_INVALID = 0,
    REFS_STREAM_SNAPSHOT_OPERATION_CREATE = 1,
    REFS_STREAM_SNAPSHOT_OPERATION_LIST = 2,
    REFS_STREAM_SNAPSHOT_OPERATION_QUERY_DELTAS = 3,
    REFS_STREAM_SNAPSHOT_OPERATION_REVERT = 4,
    REFS_STREAM_SNAPSHOT_OPERATION_SET_SHADOW_BTREE = 5,
    REFS_STREAM_SNAPSHOT_OPERATION_CLEAR_SHADOW_BTREE = 6,
    REFS_STREAM_SNAPSHOT_OPERATION_MAX = 6
} REFS_STREAM_SNAPSHOT_OPERATION, *PREFS_STREAM_SNAPSHOT_OPERATION;

enum _LCN_WEAK_REFERENCE_STATE {
    LCN_WEAK_REFERENCE_VALID = 1,
    LCN_WEAK_REFERENCE_BROKEN = 2,
    LCN_CHECKSUM_VALID = 4,
    LCN_IS_VALID = 8,
    LCN_IS_STREAM_RESERVED = 16,
    LCN_IS_READ_ONLY = 32
};

typedef enum _REFS_COMPRESSION_FORMATS {
    REFS_COMPRESSION_FORMAT_UNCHANGED = 0,
    REFS_COMPRESSION_FORMAT_UNKNOWN = 1,
    REFS_COMPRESSION_FORMAT_UNCOMPRESSED = 2,
    REFS_COMPRESSION_FORMAT_LZ4 = 3,
    REFS_COMPRESSION_FORMAT_ZSTD = 4
} REFS_COMPRESSION_FORMATS, *PREFS_COMPRESSION_FORMATS;

typedef enum _REFS_SET_VOLUME_COMPRESSION_INFO_FLAGS {
    REFS_SET_VOLUME_COMPRESSION_INFO_FLAG_START_COMPRESSION = 1,
    REFS_SET_VOLUME_COMPRESSION_INFO_FLAG_STOP_COMPRESSION = 2,
    REFS_SET_VOLUME_COMPRESSION_INFO_FLAG_GC_ONLY = 4,
    REFS_SET_VOLUME_COMPRESSION_INFO_FLAG_ENABLE_COMPRESSION = 8,
    REFS_SET_VOLUME_COMPRESSION_INFO_FLAG_DISABLE_COMPRESSION = 16,
    REFS_SET_VOLUME_COMPRESSION_INFO_FLAG_MAX = 16
} REFS_SET_VOLUME_COMPRESSION_INFO_FLAGS, *PREFS_SET_VOLUME_COMPRESSION_INFO_FLAGS;

typedef enum _REFS_QUERY_VOLUME_COMPRESSION_INFO_FLAGS {
    REFS_QUERY_VOLUME_COMPRESSION_INFO_FLAGS_RUNNING = 1,
    REFS_QUERY_VOLUME_COMPRESSION_INFO_FLAGS_STOPPED = 2
} REFS_QUERY_VOLUME_COMPRESSION_INFO_FLAGS, *PREFS_QUERY_VOLUME_COMPRESSION_INFO_FLAGS;

typedef enum _REFS_QUERY_VOLUME_IO_METRICS_INFO_QUERY_TYPE {
    REFS_QUERY_VOLUME_IO_METRICS_INFO_QUERY_TYPE_PARAMETERS = 1,
    REFS_QUERY_VOLUME_IO_METRICS_INFO_QUERY_TYPE_METRICS_DATA = 2
} REFS_QUERY_VOLUME_IO_METRICS_INFO_QUERY_TYPE, *PREFS_QUERY_VOLUME_IO_METRICS_INFO_QUERY_TYPE;

typedef enum _CSV_DOWN_LEVEL_FILE_TYPE {
    CsvDownLevelFileObject = 0,
    CsvCsvFsInternalFileObject = 1
} CSV_DOWN_LEVEL_FILE_TYPE;

typedef enum OPLOCK_NOTIFY_REASON {
    OPLOCK_NOTIFY_BREAK_WAIT_INTERIM_TIMEOUT = 0,
    OPLOCK_NOTIFY_BREAK_WAIT_TERMINATED = 1
} OPLOCK_NOTIFY_REASON;

typedef enum _SEC_TRAFFIC_SECRET_TYPE {
    SecTrafficSecret_None = 0,
    SecTrafficSecret_Client = 1,
    SecTrafficSecret_Server = 2
} SEC_TRAFFIC_SECRET_TYPE, *PSEC_TRAFFIC_SECRET_TYPE;

#endif
