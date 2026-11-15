/*
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern ULONG NtGlobalFlag;
extern ULONG NtMajorVersion;
extern ULONG NtMinorVersion;

ULONG
NTAPI
RtlGetNtGlobalFlags(VOID)
{
    return NtGlobalFlag;
}

ULONG
NTAPI
RtlGetSuiteMask(VOID)
{
    return SharedUserData->SuiteMask;
}

PCWSTR
NTAPI
RtlGetNtSystemRoot(VOID)
{
    return SharedUserData->NtSystemRoot;
}

ULONG
NTAPI
RtlGetActiveConsoleId(VOID)
{
    return ReadAcquire((PLONG)&SharedUserData->ActiveConsoleId);
}

VOID
NTAPI
RtlSetActiveConsoleId(_In_ ULONG ActiveConsoleId)
{
    InterlockedExchange((PLONG)&MmWriteableSharedUserData->ActiveConsoleId, (LONG)ActiveConsoleId);
}

ULONGLONG
NTAPI
RtlGetConsoleSessionForegroundProcessId(VOID)
{
    return ReadAcquire64((PLONG64)&SharedUserData->ConsoleSessionForegroundProcessId);
}

VOID
NTAPI
RtlSetConsoleSessionForegroundProcessId(_In_ ULONGLONG ProcessId)
{
    InterlockedExchange64((PLONG64)&MmWriteableSharedUserData->ConsoleSessionForegroundProcessId, (LONG64)ProcessId);
}

ULONG
NTAPI
RtlGetCurrentServiceSessionId(VOID)
{
    return 0;
}

BOOLEAN
NTAPI
RtlIsMultiSessionSku(VOID)
{
    return SharedUserData->DbgMultiSessionSku != 0;
}

BOOLEAN
NTAPI
RtlIsZeroMemory(
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ SIZE_T Length)
{
    const UCHAR *Bytes = Buffer;

    while (Length-- != 0)
    {
        if (*Bytes++ != 0)
            return FALSE;
    }

    return TRUE;
}

#undef RtlIsNtDdiVersionAvailable
#undef RtlIsServicePackVersionInstalled

BOOLEAN
NTAPI
RtlIsNtDdiVersionAvailable(
    _In_ ULONG Version)
{
    if (Version & 0x0000FFFF)
        return FALSE;

    return ((NtMajorVersion << 24) | (NtMinorVersion << 16)) >= Version;
}

BOOLEAN
NTAPI
RtlIsServicePackVersionInstalled(
    _In_ ULONG Version)
{
    if ((Version >> 24) != NtMajorVersion || ((Version >> 16) & 0xFF) != NtMinorVersion)
        return FALSE;

    return ((Version >> 8) & 0xFF) <= ((CmNtCSDVersion >> 8) & 0xFF);
}

NTSTATUS
__cdecl
RtlInitializeSidEx(
    _Out_writes_bytes_(SECURITY_SID_SIZE(SubAuthorityCount)) PSID Sid,
    _In_ PSID_IDENTIFIER_AUTHORITY IdentifierAuthority,
    _In_ UCHAR SubAuthorityCount,
    ...)
{
    NTSTATUS Status;
    ULONG Index;
    va_list Arguments;

    if (SubAuthorityCount > SID_MAX_SUB_AUTHORITIES)
        return STATUS_INVALID_PARAMETER;

    Status = RtlInitializeSid(Sid, IdentifierAuthority, SubAuthorityCount);
    if (!NT_SUCCESS(Status))
        return Status;

    /* The published ABI uses UCHAR as the final named parameter. Clang warns
     * about its default promotion even though the Windows ABI va-list builtin
     * handles the promoted register or stack slot. */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvarargs"
#endif
    va_start(Arguments, SubAuthorityCount);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    for (Index = 0; Index < SubAuthorityCount; ++Index)
        *RtlSubAuthoritySid(Sid, Index) = va_arg(Arguments, ULONG);
    va_end(Arguments);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlCheckTokenMembership(
    _In_opt_ HANDLE TokenHandle,
    _In_ PSID SidToCheck,
    _Out_ PBOOLEAN IsMember)
{
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    PACCESS_TOKEN Token;
    BOOLEAN CapturedSubject = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    if (IsMember == NULL || SidToCheck == NULL || !RtlValidSid(SidToCheck))
        return STATUS_INVALID_PARAMETER;

    *IsMember = FALSE;

    if (TokenHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(TokenHandle, TOKEN_QUERY, SeTokenObjectType, ExGetPreviousMode(), (PVOID *)&Token, NULL);
        if (!NT_SUCCESS(Status))
            return Status;

        SepAcquireTokenLockShared((PTOKEN)Token);
    }
    else
    {
        SeCaptureSubjectContext(&SubjectContext);
        SeLockSubjectContext(&SubjectContext);
        CapturedSubject = TRUE;
        Token = SubjectContext.ClientToken != NULL ?
                    SubjectContext.ClientToken : SubjectContext.PrimaryToken;
    }

    *IsMember = SepSidInToken((PTOKEN)Token, SidToCheck);

    if (CapturedSubject)
    {
        SeUnlockSubjectContext(&SubjectContext);
        SeReleaseSubjectContext(&SubjectContext);
    }
    else
    {
        SepReleaseTokenLock((PTOKEN)Token);
        ObDereferenceObject(Token);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
{

    PAGED_CODE();




    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
{
    {
    {



    {
    }

    {
    }


    {
    }

}

VOID
{
}

/*
 * Feature-staging API. No feature store exists, so queries report that no
 * configuration is present (callers use their built-in defaults) and the
 * usage-telemetry entry points succeed as no-ops. Win11 drivers such as
 * stornvme.sys call these during load and must not hit raising stubs.
 */
NTSTATUS
NTAPI
RtlQueryFeatureConfiguration(
    _In_ ULONG FeatureId,
    _In_ ULONG ConfigurationType,
    _Out_ PULONG64 ChangeStamp,
    _Out_ PVOID Configuration)
{
    UNREFERENCED_PARAMETER(FeatureId);
    UNREFERENCED_PARAMETER(ConfigurationType);
    UNREFERENCED_PARAMETER(Configuration);
    if (ChangeStamp)
        *ChangeStamp = 0;
    return STATUS_NOT_FOUND;
}

ULONG64
NTAPI
RtlQueryFeatureConfigurationChangeStamp(VOID)
{
    return 0;
}

NTSTATUS
NTAPI
RtlRegisterFeatureConfigurationChangeNotification(
    _In_ PVOID Callback,
    _In_opt_ PVOID Context,
    _Inout_opt_ PULONG64 ChangeStamp,
    _Out_ PHANDLE NotificationHandle)
{
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Context);
    if (ChangeStamp)
        *ChangeStamp = 0;
    if (!NotificationHandle)
        return STATUS_INVALID_PARAMETER;
    *NotificationHandle = (HANDLE)1;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlUnregisterFeatureConfigurationChangeNotification(
    _In_ HANDLE NotificationHandle)
{
    UNREFERENCED_PARAMETER(NotificationHandle);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlNotifyFeatureUsage(
    _In_ PVOID FeatureUsageReport)
{
    UNREFERENCED_PARAMETER(FeatureUsageReport);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlRecordFeatureUsage(
    _In_ ULONG FeatureId,
    _In_ ULONG Kind,
    _In_ ULONG Addend,
    _In_opt_ PVOID Reserved)
{
    UNREFERENCED_PARAMETER(FeatureId);
    UNREFERENCED_PARAMETER(Kind);
    UNREFERENCED_PARAMETER(Addend);
    UNREFERENCED_PARAMETER(Reserved);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlRegisterFeatureUsageProvider(
    _In_ PVOID Registration,
    _Out_ PHANDLE ProviderHandle)
{
    UNREFERENCED_PARAMETER(Registration);
    if (!ProviderHandle)
        return STATUS_INVALID_PARAMETER;
    *ProviderHandle = (HANDLE)1;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlUnregisterFeatureUsageProvider(
    _In_ HANDLE ProviderHandle)
{
    UNREFERENCED_PARAMETER(ProviderHandle);
    return STATUS_SUCCESS;
}

VOID
NTAPI
RtlArmFeatureUsageProviderFlushNotification(
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
}

/* EOF */
