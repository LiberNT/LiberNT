/*
 * PROJECT:     LiberNT NT User-Mode DLL
 * PURPOSE:     Capability and service SID support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 */

#include <ntdll.h>
#include <symcrypt.h>

SYMCRYPT_CPU_FEATURES SYMCRYPT_CALL
SymCryptCpuFeaturesNeverPresent(VOID)
{
    return 0;
}

VOID SYMCRYPT_CALL
SymCryptFatal(UINT32 FatalCode)
{
    UNREFERENCED_PARAMETER(FatalCode);
}

VOID SYMCRYPT_CALL
SymCryptInjectError(PBYTE Buffer, SIZE_T Size)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Size);
}

#if SYMCRYPT_CPU_X86 | SYMCRYPT_CPU_AMD64
SYMCRYPT_ERROR SYMCRYPT_CALL
SymCryptSaveXmm(PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
    return SYMCRYPT_NO_ERROR;
}

VOID SYMCRYPT_CALL
SymCryptRestoreXmm(PSYMCRYPT_EXTENDED_SAVE_DATA SaveArea)
{
    UNREFERENCED_PARAMETER(SaveArea);
}
#endif

NTSTATUS
NTAPI
RtlDeriveCapabilitySidsFromName(
    _In_ PUNICODE_STRING CapabilityName,
    _Out_ PSID CapabilityGroupSid,
    _Out_ PSID CapabilitySid)
{
    static const SID_IDENTIFIER_AUTHORITY AppAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};
    static const SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    UNICODE_STRING UppercaseName;
    NTSTATUS Status;
    ULONG Hash[8];
    PISID Sid;

    Status = RtlUpcaseUnicodeString(&UppercaseName, CapabilityName, TRUE);
    if (!NT_SUCCESS(Status))
        return Status;

    SymCryptSha256((PBYTE)UppercaseName.Buffer, UppercaseName.Length, (PBYTE)Hash);
    RtlFreeUnicodeString(&UppercaseName);

    Sid = CapabilitySid;
    Sid->Revision = SID_REVISION;
    Sid->IdentifierAuthority = AppAuthority;
    Sid->SubAuthorityCount = 2 + RTL_NUMBER_OF(Hash);
    *RtlSubAuthoritySid(Sid, 0) = SECURITY_BATCH_RID;
    *RtlSubAuthoritySid(Sid, 1) = SECURITY_CAPABILITY_APP_RID;
    RtlCopyMemory(Sid->SubAuthority + 2, Hash, sizeof(Hash));

    Sid = CapabilityGroupSid;
    Sid->Revision = SID_REVISION;
    Sid->IdentifierAuthority = NtAuthority;
    Sid->SubAuthorityCount = 1 + RTL_NUMBER_OF(Hash);
    *RtlSubAuthoritySid(Sid, 0) = SECURITY_BUILTIN_DOMAIN_RID;
    RtlCopyMemory(Sid->SubAuthority + 1, Hash, sizeof(Hash));

    return STATUS_SUCCESS;
}

C_ASSERT(SYMCRYPT_SHA1_RESULT_SIZE == (SECURITY_SERVICE_ID_RID_COUNT - 1) * sizeof(ULONG));

NTSTATUS
NTAPI
RtlCreateServiceSid(
    _In_ PUNICODE_STRING ServiceName,
    _Out_writes_bytes_opt_(*ServiceSidLength) PSID ServiceSid,
    _Inout_ PULONG ServiceSidLength)
{
    static const SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    UNICODE_STRING UppercaseName;
    NTSTATUS Status;
    ULONG Length;
    PISID Sid;

    if (!ServiceName || !ServiceSidLength)
        return STATUS_INVALID_PARAMETER;

    Length = RtlLengthRequiredSid(SECURITY_SERVICE_ID_RID_COUNT);
    if (!ServiceSid || *ServiceSidLength < Length)
    {
        *ServiceSidLength = Length;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Status = RtlUpcaseUnicodeString(&UppercaseName, ServiceName, TRUE);
    if (!NT_SUCCESS(Status))
        return Status;

    Sid = ServiceSid;
    Sid->Revision = SID_REVISION;
    Sid->IdentifierAuthority = NtAuthority;
    Sid->SubAuthorityCount = SECURITY_SERVICE_ID_RID_COUNT;
    *RtlSubAuthoritySid(Sid, 0) = SECURITY_SERVICE_ID_BASE_RID;
    SymCryptSha1((PBYTE)UppercaseName.Buffer, UppercaseName.Length, (PBYTE)(Sid->SubAuthority + 1));
    RtlFreeUnicodeString(&UppercaseName);

    *ServiceSidLength = Length;
    return STATUS_SUCCESS;
}
